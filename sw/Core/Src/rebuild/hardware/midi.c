#include "midi.h"
#include "gfx/gfx.h"
#include "midi_defs.h"
#include "touchstrips.h"
#include "tusb.h"

// needs cleaning up
extern Preset rampreset;
extern bool got_ui_reset;
extern u8 playmode;
extern volatile u8 gotclkin;
extern s8 arpmode;
extern u8 arpbits;
extern Voice voices[8];
extern u8 synthfingerdown_nogatelen_internal;
extern u16 memory_position[8];

extern int GetParam(u8 paramidx, u8 mod);
extern void SetPreset(u8 preset, bool force);
extern void EditParamNoQuant(u8 paramidx, u8 mod, s16 data);
extern void ShowMessage(Font fnt, const char* msg, const char* submsg);
extern void OnLoop(void);
extern Touch* touch_synth_getlatest(int finger);
extern Touch* touch_synth_getprev(int finger);
extern int string_center_pitch(u8 fi);
extern int string_pitch_at_pad(u8 fi, u8 pad);
extern int param_eval_finger(u8 paramidx, int fingeridx, Touch* f);
// -- needs cleaning up

// midi uart, lives in main.c
extern UART_HandleTypeDef huart3;

// these should all live in the synth, as they all define the properties of a voice moreso than a property of midi
// itself
u8 midi_pressure_override = 0; // true if midi note is pressed
u8 midi_pitch_override = 0;    // true if midi note is sounded out, includes release phase
u8 midi_suppress = 0;          // true if midi is suppressed by touch / latch / sequencer note
u8 midi_notes[NUM_VOICES];
u16 midi_positions[NUM_VOICES];
u8 midi_velocities[NUM_VOICES];
u8 midi_goal_note[NUM_VOICES];
u8 midi_channels[NUM_VOICES] = {255, 255, 255, 255, 255, 255, 255, 255};
u8 midi_poly_pressure[NUM_VOICES];
// -- synth

#define NUM_14BIT_CCS 32
#define MIDI_BUFFER_SIZE 16

u8 midi_chan_pressure[NUM_MIDI_CHANNELS];
s16 midi_chan_pitchbend[NUM_MIDI_CHANNELS];

// buffers
static u8 midi_receive_buffer[MIDI_BUFFER_SIZE];
static u8 midi_send_buffer[2 * MIDI_BUFFER_SIZE];
static u8 midi_send_head;
static u8 midi_send_tail;

void midi_init(void) {
	// serial
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, sizeof(midi_receive_buffer));
	// usb
	tusb_init();
}

// === SYNTH UTILITIES === //

static u8 find_midi_note(u8 chan, u8 note) {
	for (int string_id = 0; string_id < 8; ++string_id)
		if ((midi_pitch_override & (1 << string_id)) && midi_notes[string_id] == note
		    && midi_channels[string_id] == chan)
			return string_id;
	return 255;
}

static u8 find_string_for_midi_pitch(int midi_pitch) {
	// pitch falls below center of bottom string
	if (midi_pitch < string_center_pitch(0))
		return 0;
	// pitch falls above center of top string
	if (midi_pitch >= string_center_pitch(7))
		return 7;
	// find the string with the closest center pitch
	u8 desired_string = 0;
	s32 min_dist = 2147483647; // int max
	for (u8 i = 0; i < 8; i++) {
		u32 pitch_dist = abs(string_center_pitch(i) - midi_pitch);
		if (pitch_dist < min_dist) {
			min_dist = pitch_dist;
			desired_string = i;
		}
	}
	return desired_string;
}

// return the position of the highest pad the midi pitch is higher than - or equal to
static u16 find_string_position_for_midi_pitch(u8 string_id, int midi_pitch) {
	// top pad has pad_id 0
	// pad spacing is 256 per pad, position = pad_id << 8
	for (u8 pad = 7; pad > 0; pad--) {
		if (midi_pitch >= string_pitch_at_pad(string_id, pad)) {
			return (7 - pad) << 8;
		}
	}
	// if the pitch was lower than the pitch of pad 1, we return the bottom pad
	return 7 << 8;
}

static u8 find_free_midi_string(u8 midi_note_number, u16* midi_note_position) {
	Touch* synthf = touch_synth_getlatest(0);
	s32 midi_pitch =
	    // base pitch
	    12 * ((param_eval_finger(P_OCT, 0, synthf) << 9) + (param_eval_finger(P_PITCH, 0, synthf) >> 7)) +
	    // pitch
	    ((midi_note_number - 24) << 9);

	// find the best string for this midi note
	u8 desired_string = find_string_for_midi_pitch(midi_pitch);

	// try to find:
	// 1. the non-sounding string closest to our desired string
	// 2. the sounding string that is the quietest
	u8 string_option[8];
	u8 num_string_options = 0;
	u8 min_string_dist = 255;
	float min_vol = __FLT_MAX__;
	u8 min_string_id = 255;

	// collect non-sounding strings
	for (u8 string_id = 0; string_id < 8; string_id++) {
		if (voices[string_id].vol < 0.001f) {
			string_option[num_string_options] = string_id;
			num_string_options++;
		}
	}
	// find closest
	for (u8 option_id = 0; option_id < num_string_options; option_id++) {
		if (abs(string_option[option_id] - desired_string) < min_string_dist) {
			min_string_dist = abs(string_option[option_id] - desired_string);
			min_string_id = string_option[option_id];
		}
	}
	// return closest, if found
	if (min_string_dist != 255) {
		// collect the position on the string before returning
		*midi_note_position = find_string_position_for_midi_pitch(min_string_id, midi_pitch);
		return min_string_id;
	}
	// collect non-pressed strings
	num_string_options = 0;
	for (u8 string_id = 0; string_id < 8; string_id++) {
		if (!(synthfingerdown_nogatelen_internal & (1 << string_id))) {
			string_option[num_string_options] = string_id;
			num_string_options++;
		}
	}
	// find quietest
	for (u8 option_id = 0; option_id < num_string_options; option_id++) {
		if (voices[string_option[option_id]].vol < min_vol) {
			min_vol = voices[string_option[option_id]].vol;
			min_string_id = string_option[option_id];
		}
	}
	// collect the position on the string before returning
	if (min_string_id != 255) {
		*midi_note_position = find_string_position_for_midi_pitch(min_string_id, midi_pitch);
	}
	// return quietest - this returns 255 if nothing was found
	return min_string_id;
}

// === OUTPUT LOOP === //

// throw send buffer to the uart
static void midi_buffer_to_uart(void) {
	if (huart3.TxXferCount == 0 && midi_send_head != midi_send_tail) {
		u8 from = midi_send_tail & 15;
		u8 to = midi_send_head & 15;
		if (to > from) {
			midi_send_tail += (to - from);
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, to - from);
		}
		else if (to < from) {
			// wrapped! send from->16, and 0->to
			u8 send_len = (MIDI_BUFFER_SIZE - from) + to;
			// copy looped part to end so we can send it all in one go! good on us.
			memcpy(midi_send_buffer + MIDI_BUFFER_SIZE, midi_send_buffer, to);
			midi_send_tail += send_len;
			HAL_UART_Transmit_DMA(&huart3, midi_send_buffer + from, send_len);
		}
	}
}

// add midi bytes to the send buffer
static bool send_midi_serial(const u8* data, int len) {
	if (len <= 0)
		return true;
	if (midi_send_head - midi_send_tail + len > MIDI_BUFFER_SIZE)
		return false; // full
	while (len--)
		midi_send_buffer[(midi_send_head++) & 15] = *data++;
	return true;
}

// send midi msg to uart and usb, returns false if serial too full
static bool send_midi_msg(u8 status, u8 data1, u8 data2) {
	// prepare message
	u8 num_bytes = 3;
	if (status == MIDI_PROGRAM_CHANGE || status == MIDI_CHANNEL_PRESSURE)
		num_bytes = 2;
	int midi_ch_out = clampi((mini(GetParam(P_MIDI_CH_OUT, 0), FULL - 1) * 16) / FULL, 0, 15);
	if (status < MIDI_SYSTEM_EXCLUSIVE)
		status += midi_ch_out; // set output channel
	u8 buf[4] = {status >> 4, status, data1, data2};
	// send to serial
	if (!send_midi_serial(buf + 1, num_bytes)) {
		return false;
	}
	// if serial was successful, send to usb
	// we assume this succeeds as it's so much faster than serial midi
	tud_midi_packet_write(buf);
	return true;
}

// outgoing midi gets processed once and sent out identically to serial and usb
void process_all_midi_out(void) {
	static u8 string_id = 0;
	static u8 note[NUM_VOICES];             // last sent midi note
	static u8 note_on_pressure[NUM_VOICES]; // pressure/velocity sent on note on
	static u8 aftertouch[NUM_VOICES];       // last sent poly aftertouch
	static u8 position[NUM_VOICES];         // last sent position CC
	static u8 pressure[NUM_VOICES];         // last sent pressure CC

	// exit if the uart is not ready
	if (huart3.TxXferCount)
		return;
	// we check for a maximum of eight times (once for each voice)
	u8 num_loops = 0;
	while (num_loops < 8) {
		num_loops++;
		//  get a bunch of parameters from the synth
		Touch* synthf = touch_synth_getlatest(string_id);
		Touch* prevsynthf = touch_synth_getprev(string_id);
		bool pres_stable = abs(prevsynthf->pres - synthf->pres) < 100;
		bool pos_stable = abs(prevsynthf->pos - synthf->pos) < 32;
		bool pres_significant = synthf->pres > 200;
		// these are our targets
		u8 target_note = midi_goal_note[string_id];
		u8 target_pressure = clampi((synthf->pres - 100) / 48, 0, 127);

		// take out some undesired note/pressure values
		if (!target_note)
			target_pressure = 0;
		if (arpmode >= 0 && !(arpbits & (1 << string_id)))
			target_pressure = 0;
		if (!target_pressure)
			target_note = 0;

		// one loop checks all needed outgoing midi messages for one voice
		bool sent = false;
		u8 cur_note = note[string_id];
		// note has changed
		if (target_note != cur_note) {
			// we were playing a note => send note off
			if (cur_note) {
				if (!send_midi_msg(MIDI_NOTE_OFF, cur_note, 0))
					return; // if the buffer is full, we exit the function and try again next time
				note[string_id] = 0;
				aftertouch[string_id] = 0;
				sent = true;
			}
			// we start playing a new note => send note on
			if (target_note != 0 && pos_stable && pres_stable) {
				// we use the current pressure as the note velocity
				if (!send_midi_msg(MIDI_NOTE_ON, target_note, target_pressure))
					return;
				note[string_id] = target_note;
				note_on_pressure[string_id] = target_pressure;
				aftertouch[string_id] = 0;
				sent = true;
			}
		}
		// we define aftertouch as any pressure on top of the pressure when the note started
		u8 goal_aftertouch = maxi(target_pressure - note_on_pressure[string_id], 0);
		if (abs(goal_aftertouch - aftertouch[string_id]) > 4) {
			// poly aftertouch (only when pressure difference is larger than 4)
			if (!send_midi_msg(MIDI_POLY_KEY_PRESSURE, cur_note, goal_aftertouch))
				return;
			aftertouch[string_id] = goal_aftertouch;
			sent = true;
		}
		// voice position, CC32 - CC39
		u8 goal_position = clampi(127 - (synthf->pos / 13 - 16), 0, 127);
		if (abs(goal_position - position[string_id]) > 1 && pres_significant && pres_stable) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 32 + string_id, goal_position))
				return;
			position[string_id] = goal_position;
			sent = true;
		}
		// voice pressure, CC40 - CC47
		if (abs(target_pressure - pressure[string_id]) > 1) {
			if (!send_midi_msg(MIDI_CONTROL_CHANGE, 40 + string_id, target_pressure))
				return;
			pressure[string_id] = target_pressure;
			sent = true;
		}

		// jump to next voice
		string_id = (string_id + 1) & 7;

		// if we sent any midi message for this voice, send the midi buffer to the uart and exit the function
		// the next time we enter the function again, string_id will be set to the next voice
		if (sent) {
			midi_buffer_to_uart();
			return;
		}
	}
}

// === INPUT LOOP === //

// apply midi messages to plinky
static void process_midi_msg(u8 status, u8 d1, u8 d2) {
	static u8 cc14_lsb[NUM_14BIT_CCS];

	u8 chan = status & 0x0F; // save the channel
	u8 type = status & 0xF0; // take the channel out
	u8 midi_ch_in = clampi((mini(GetParam(P_MIDI_CH_IN, 0), FULL - 1) * 16) / FULL, 0, 15);

	// allow only selected channel and system msgs
	if ((chan != midi_ch_in) && (type != MIDI_SYSTEM_COMMON_MSG))
		return;

	// turn silent note ons into note offs
	if (type == MIDI_NOTE_ON && d2 == 0)
		type = MIDI_NOTE_OFF;

	switch (type) {
	case MIDI_PROGRAM_CHANGE:
		if (d1 < 32)
			SetPreset(d1, false);
		break;
	case MIDI_NOTE_OFF: {
		// find string with existing midi note
		u8 string_id = find_midi_note(chan, d1);
		if (string_id < 8) {
			midi_pressure_override &= ~(1 << string_id);
		}
	} break;
	case MIDI_NOTE_ON: {
		u16 note_position;
		// find string with existing midi note
		u8 string_id = find_midi_note(chan, d1);
		// none found - find empty string
		if (string_id == 255) {
			string_id = find_free_midi_string(d1, &note_position);
			if (string_id < NUM_VOICES)
				midi_positions[string_id] = note_position;
		}
		// set midi values
		if (string_id < 8) {
			midi_notes[string_id] = d1;
			midi_channels[string_id] = chan;
			midi_velocities[string_id] = d2;
			midi_poly_pressure[string_id] = 0;
			midi_pressure_override |= 1 << string_id;
			midi_pitch_override |= 1 << string_id;
		}
	} break;
	case MIDI_PITCH_BEND:
		midi_chan_pitchbend[chan] = (d1 + (d2 << 7)) - 0x2000;
		break;
	case MIDI_POLY_KEY_PRESSURE: {
		u8 string_id = find_midi_note(chan, d1);
		if (string_id < 8) {
			midi_poly_pressure[string_id] = d2;
		}
	} break;
	case MIDI_CHANNEL_PRESSURE: {
		midi_chan_pressure[chan] = d1;
	} break;
	case MIDI_CONTROL_CHANGE: {
		if (d1 >= NUM_14BIT_CCS && d1 < (2 * NUM_14BIT_CCS))
			cc14_lsb[d1 - 32] = d2;
		s8 param = (d1 < 128) ? midi_cc_table[d1] : -1;
		if (param >= 0 && param < P_LAST) {
			int val;
			if (d1 < 32)
				val = (d2 << 7) + cc14_lsb[d1]; // full CC14
			else
				val = (d2 << 7);
			val = (val * FULL) / (127 * 128 + 127); // map to plinky space
			if (param_flags[param] & FLAG_SIGNED)
				val = val * 2 - FULL;
			EditParamNoQuant(param, M_BASE, val); // set parameter

			if (param == P_ARPONOFF) { // this should be moved to arp/ui
				if (val > 64) {
					rampreset.flags = rampreset.flags | FLAGS_ARP;
				}
				else {
					rampreset.flags = rampreset.flags & ~FLAGS_ARP;
				}
				ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_ARP)) ? "arp on" : "arp off", 0);
			}
			if (param == P_LATCHONOFF) { // this should be moved to latch/ui
				if (val > 64) {
					rampreset.flags = rampreset.flags | FLAGS_LATCH;
				}
				else {
					rampreset.flags = rampreset.flags & ~FLAGS_LATCH;
				}
				ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_LATCH)) ? "latch on" : "latch off", 0);
			}
		}
		break;
	}
	case MIDI_SYSTEM_COMMON_MSG: { // system msgs, use full status
		static u8 clock_24ppqn;
		switch (status) {
		case MIDI_START:
			got_ui_reset = true;
			clock_24ppqn = 5; // 2020-02-26: Used to be 0, changed to 5:
			                  // https://discord.com/channels/784856175937585152/784884878994702387/814951459581067264
			playmode = PLAYING;
			break;
		case MIDI_CONTINUE:
			playmode = PLAYING;
			break;
		case MIDI_STOP:
			clock_24ppqn = 0;
			playmode = PLAY_STOPPED;
			OnLoop();
			break;
		case MIDI_TIMING_CLOCK:
			// midi clock! 24ppqn, we want 4, so divide by 6.
			clock_24ppqn++;
			if (clock_24ppqn == 6) {
				gotclkin++;
				clock_24ppqn = 0;
			}
			break;
		}
		break;
	}
	}
}

// read bytes from *buf and put them into midi messages
static void midi_bytes_to_msg(const u8* buf, u8 len) {
	static u8 state = 0;
	static u8 msg[3] = {0};
	for (; len--;) {
		u8 data = *buf++;
		// status byte
		if (data & 0x80) {
			// real-time msg
			if ((data & 0xF8) == 0xF8) {
				// handle immediately
				process_midi_msg(data, 0, 0);
			}
			// channel mode msg
			else if ((data & 0xF0) == 0xF0) {
				// cancels running status, no further processing
				msg[0] = 0;
			}
			// channel voice msg, start new
			else {
				msg[0] = data;
				state = 1;
			}
		}
		// data byte
		else {
			// not gathering a channel voice msg, ignore
			if (msg[0] == 0) {
				continue;
			}
			// running status
			if (state == 3) {
				state = 1;
			}
			// save data
			msg[state++] = data;
			// program change and channel pressure only have one data byte
			if (state == 2 && ((msg[0] & 0xF0) == MIDI_PROGRAM_CHANGE || (msg[0] & 0xF0) == MIDI_CHANNEL_PRESSURE)) {
				process_midi_msg(msg[0], msg[1], 0);
			}
			// we received a full midi msg, process
			else if (state == 3) {
				process_midi_msg(msg[0], msg[1], msg[2]);
			}
		}
	}
}

// handles up to one buffer (16 bytes) of incoming serial midi data
void process_serial_midi_in(void) {
	// pass the midi out buffer to the uart
	// rj: why is this in the midi in function?
	midi_buffer_to_uart();
	// get midi bytes from buffer and pass them to midi_bytes_to_msg()
	static u8 last_read_pos = 0;
	u8 read_pos = MIDI_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx);
	if (read_pos != last_read_pos) {
		if (read_pos > last_read_pos) {
			midi_bytes_to_msg(&midi_receive_buffer[last_read_pos], read_pos - last_read_pos);
		}
		else {
			midi_bytes_to_msg(&midi_receive_buffer[last_read_pos], MIDI_BUFFER_SIZE - last_read_pos);
			midi_bytes_to_msg(&midi_receive_buffer[0], read_pos);
		}
	}
	last_read_pos = read_pos;
}

void process_usb_midi_in(void) {
	const static u8 max_packets_per_call = 2;
	u8 midi_packet[4];
	u8 packets_handled = 0;
	do {
		if (!tud_midi_available() || !tud_midi_packet_read(midi_packet))
			return;
		process_midi_msg(midi_packet[1], midi_packet[2], midi_packet[3]);
		packets_handled++;
	} while (packets_handled < max_packets_per_call);
}

// == AUX == //

// panic
static void midi_panic(void) {
	midi_pressure_override = 0, midi_pitch_override = 0;
	memset(midi_notes, 0, sizeof(midi_notes));
	memset(midi_velocities, 0, sizeof(midi_velocities));
	memset(midi_poly_pressure, 0, sizeof(midi_poly_pressure));
	memset(midi_channels, 255, sizeof(midi_channels));
	memset(midi_chan_pressure, 0, sizeof(midi_chan_pressure));
	memset(midi_chan_pitchbend, 0, sizeof(midi_chan_pitchbend));
}

// from https://community.st.com/s/question/0D50X00009XkflR/haluartirqhandler-bug
// what a trash fire
// USART Error Handler
void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
	__HAL_UART_CLEAR_OREFLAG(huart);
	__HAL_UART_CLEAR_NEFLAG(huart);
	__HAL_UART_CLEAR_FEFLAG(huart);
	/* Disable the UART Error Interrupt: (Frame error, noise error, overrun error) */
	__HAL_UART_DISABLE_IT(huart, UART_IT_ERR);
	// The most important thing when UART framing error occur/any error is restart the RX process
	midi_panic();
	HAL_UART_Receive_DMA(&huart3, midi_receive_buffer, sizeof(midi_receive_buffer));
}