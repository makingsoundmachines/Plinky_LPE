#include "hardware/touchstrips.h"
#include "hardware/adc_dac.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

typedef struct CalibProgress {
	float weight[8];
	float pos[8];
	float pres[8];
} CalibProgress;
static inline CalibProgress* GetCalibProgress(int sensoridx) {
	CalibProgress* p = (CalibProgress*)delaybuf;
	return p + sensoridx;
}
CalibResult calibresults[18];

Touch fingers_synth_time[8][8]; // 8 frames for 8 fingers
Touch fingers_synth_sorted[8][8];
volatile u8 finger_frame_synth;

typedef struct euclid_state {
	int trigcount;
	bool did_a_retrig;
	bool supress;

} euclid_state;

euclid_state arp_rhythm;
euclid_state seq_rhythm;

static inline u8 touch_synth_writingframe(void) {
	return (finger_frame_synth) & 7;
}
static inline u8 touch_synth_frame(void) {
	return (finger_frame_synth - 1) & 7;
}
static inline u8 touch_synth_prevframe(void) {
	return (finger_frame_synth - 2) & 7;
}

static inline Touch* touch_synth_getlatest(int finger) {
	return &fingers_synth_time[finger][touch_synth_frame()];
}
static inline Touch* touch_synth_getprev(int finger) {
	return &fingers_synth_time[finger][touch_synth_prevframe()];
}
static inline Touch* touch_synth_getwriting(int finger) {
	return &fingers_synth_time[finger][touch_synth_writingframe()];
}

#define SWAP(a, b)                                                                                                     \
	if (a > b) {                                                                                                       \
		int t = a;                                                                                                     \
		a = b;                                                                                                         \
		b = t;                                                                                                         \
	}
void sort8(int* dst, const int* src) {
	int a0 = src[0], a1 = src[1], a2 = src[2], a3 = src[3], a4 = src[4], a5 = src[5], a6 = src[6], a7 = src[7];
	SWAP(a0, a1);
	SWAP(a2, a3);
	SWAP(a4, a5);
	SWAP(a6, a7);
	SWAP(a0, a2);
	SWAP(a1, a3);
	SWAP(a4, a6);
	SWAP(a5, a7);
	SWAP(a1, a2);
	SWAP(a5, a6);
	SWAP(a0, a4);
	SWAP(a3, a7);
	SWAP(a1, a5);
	SWAP(a2, a6);
	SWAP(a1, a4);
	SWAP(a3, a6);
	SWAP(a2, a4);
	SWAP(a3, a5);
	SWAP(a3, a4);
	dst[0] = a0;
	dst[1] = a1;
	dst[2] = a2;
	dst[3] = a3;
	dst[4] = a4;
	dst[5] = a5;
	dst[6] = a6;
	dst[7] = a7;
}
#undef SWAP

void check_curstep(void) { // enforces invariants
	if (rampreset.looplen_step <= 0 || rampreset.looplen_step > 64)
		rampreset.looplen_step = 64;
	rampreset.loopstart_step_no_offset &= 63;
	u8 loopstart_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;
	cur_step = (cur_step - loopstart_step) % rampreset.looplen_step;
	if (cur_step < 0)
		cur_step += rampreset.looplen_step;
	cur_step += loopstart_step;
}

void set_cur_step(u8 newcurstep, bool triggerit) {
	cur_step = newcurstep;
	check_curstep();
	seq_rhythm.did_a_retrig = triggerit; // make the sound play out once
	ticks_since_step = 0;
	seq_divide_counter = 0;
}

void OnLoop(void) {
	if (pending_preset != 255) {
		SetPreset(pending_preset, false);
		pending_preset = 255;
	}
	if (pending_pattern != 255) {
		EditParamQuant(P_SEQPAT, M_BASE, pending_pattern);
		pending_pattern = 255;
	}
	if (pending_loopstart_step != 255) {
		u8 loopstart_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;
		if (loopstart_step != pending_loopstart_step) {
			rampreset.loopstart_step_no_offset = (pending_loopstart_step - step_offset) & 63;
			ramtime[GEN_PRESET] = millis();
		}
		set_cur_step(loopstart_step, seq_rhythm.did_a_retrig);
		pending_loopstart_step = 255;
	}
	if (pending_sample1 != cur_sample1 && pending_sample1 != 255) {
		EditParamQuant(P_SAMPLE, 0, pending_sample1);
		pending_sample1 = 255;
	}
	check_curstep();
}

bool got_ui_reset = false;
int tap_count = 0;
void clear_latch(void);

void reverb_clear(void) {
	memset(reverbbuf, 0, (RVMASK + 1) * 2);
}
void delay_clear(void) {
	memset(delaybuf, 0, (DLMASK + 1) * 2);
}

u16 audioin_holdtime = 0;
s16 audioin_peak = 0;
s16 audioin_hold = 0;
ValueSmoother recgain_smooth;
int audiorec_gain_target = 1 << 15;

int recpos = 0;      // this cycles around inside the delay buffer (which we use for a recording buffer) while armed...
int recstartpos = 0; // once we start recording, we note the position in the buffer here
int recreadpos = 0;  // ...and this is where we are up to in terms of reading that out and writing it to flash
u8 recsliceidx = 0;
const bool pre_erase = true;
u32 record_flashaddr_base = 0;

SampleInfo* getrecsample(void) {
	return &ramsample;
}
static inline u8 getwaveform4(SampleInfo* s, int x) { // x is 0-2047
	if (x < 0 || x >= 2048)
		return 0;
	return (s->waveform4_b[x >> 1] >> ((x & 1) * 4)) & 15;
}
static inline u8 getwaveform4halfres(SampleInfo* s, int x) { // x 0-1023
	u8 b = s->waveform4_b[x & 1023];
	return maxi(b & 15, b >> 4);
}
static inline u16 getwaveform4zoom(SampleInfo* s, int x, int zoom) { // x is 0-2048. returns average and peak!
	if (zoom <= 0)
		return getwaveform4(s, x >> zoom);
	int samplepairs = 1 << (zoom - 1);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	int avg = 0, peak = 0;
	u8* bend = &s->waveform4_b[1024];
	for (int i = 0; i < samplepairs && b < bend; ++i, ++b) {
		int s0 = b[0] & 15;
		int s1 = b[0] >> 4;
		avg += s0 + s1;
		peak = maxi(peak, maxi(s0, s1));
	}
	avg >>= zoom;
	return avg + peak * 256;
}

static inline void setwaveform4(SampleInfo* s, int x, int v) {
	v = clampi(v, 0, 15);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	if (x & 1) {
		v = maxi(v, (*b) >> 4);
		*b = (*b & 0x0f) | (v << 4);
	}
	else {
		v = maxi(v, (*b) & 15);
		*b = (*b & 0xf0) | v;
	}
}

void DebugSPIPage(int addr);

void recording_stop_really(void) {
	// clear out the raw audio in the delaybuf
	reverb_clear();
	delay_clear();
	ramtime[GEN_SAMPLE] = millis(); // fill in the remaining split points
	SampleInfo* s = getrecsample();
	int startsamp = s->splitpoints[recsliceidx];
	int endsamp = s->samplelen;
	int n = 8 - recsliceidx;
	for (int i = recsliceidx + 1; i < 8; ++i) {
		int samp = startsamp + ((endsamp - startsamp) * (i - recsliceidx)) / n;
		s->splitpoints[i] = samp;
		EmuDebugLog("POST RECORD EVEN SET SPLITPOINT %d to %d\n", i, s->splitpoints[i]);
	}
	recsliceidx = 0;
	ramtime[GEN_SAMPLE] = millis();
	enable_audio = EA_PLAY;
}

void recording_stop(void) {
	if (enable_audio == EA_PLAY) {
		ui_mode = UI_DEFAULT;
	}
	else if (enable_audio == EA_RECORDING) {
		enable_audio = EA_STOPPING1;
	}
	else if (enable_audio >= EA_STOPPING1) {
		// do nothing
	}
	else
		enable_audio = EA_PLAY;
}

void seq_step(int initial);

void recording_trigger(void) {
	recsliceidx = 0;
	SampleInfo* s = getrecsample();
	memset(s, 0, sizeof(SampleInfo));
#define LEADIN 1024
	int leadin = mini(recpos, LEADIN);
	recreadpos = recstartpos = recpos - leadin;
	s->samplelen = 0;
	s->splitpoints[0] = leadin;
	enable_audio = EA_RECORDING;
}

void arp_reset(void);
void ShowMessage(Font fnt, const char* msg, const char* submsg);

void toggle_arp(void);

int prev_prev_total_ui_pressure = 0;
int prev_total_ui_pressure = 0;
int total_ui_pressure = 0;

typedef struct FingerStorage {
	u8 minpos, maxpos;
	u8 avgvel;
} FingerStorage;
FingerStorage latch[8];
void clear_latch(void) {
	memset(latch, 0, sizeof(latch));
}

static inline int randrange(int mn, int mx) {
	return mn + (((rand() & 255) * (mx - mn)) >> 8);
}

int param_eval_finger(u8 paramidx, int fingeridx, Touch* f);
u8 synthfingerdown_nogatelen_internal;
u8 physical_touch_finger = 0;
bool read_from_seq = false;

FingerRecord* readpattern(int fi) {
	if (rampattern_idx != cur_pattern || shift_state == SS_CLEAR)
		return 0;
	FingerRecord* fr = &rampattern[(cur_step >> 4) & 3].steps[cur_step & 15][fi];
	// does any substep hold data?
	int record_pressure = 0;
	for (u8 i = 0; i < 8; i++)
		record_pressure += fr->pres[i];
	return record_pressure == 0 ? 0 : fr;
}

/////////////////// XXXXX MIDI HERE?
// bitmask of 'midi pitch override' and 'midi pressure override'
// midipitch
// here: if real pressure, clear both
// here: if pressure override, set it as midi channel aftertouch + midi note pressure
// in plinky.c midi note down: inc next voice index; set both override bits; set voice midi pitch and channel and
// velocity in plinky.c midi note up: clear pressure override bit in the synth - override pitch if bit is set
u8 midi_pressure_override = 0; // true if midi note is pressed
u8 midi_pitch_override = 0;    // true if midi note is sounded out, includes release phase
u8 midi_suppress = 0;          // true if midi is suppressed by touch / latch / sequencer note
int memory_position[8];
u8 midi_notes[8];
int midi_positions[8];
u8 midi_velocities[8];
u8 midi_aftertouch[8];
u8 midi_channels[8] = {255, 255, 255, 255, 255, 255, 255, 255};
u8 midi_chan_aftertouch[16];
s16 midi_chan_pitchbend[16];
u8 midi_next_finger;
u8 midi_lsb[32];
u8 find_midi_note(u8 chan, u8 note) {
	for (int fi = 0; fi < 8; ++fi)
		if ((midi_pitch_override & (1 << fi)) && midi_notes[fi] == note && midi_channels[fi] == chan)
			return fi;
	return 255;
}

int stride(u32 scale, int stride_semitones, int fingeridx);

int string_pitch_at_pad(u8 fi, u8 pad) {
	Touch* synthf = touch_synth_getlatest(fi);
	u32 scale = param_eval_finger(P_SCALE, fi, synthf);
	// pitch calculation:
	return
	    // calculate pitch offset, based on
	    lookupscale(
	        // the scale
	        scale,
	        // the step-offset set by "degree"
	        param_eval_finger(P_ROTATE, fi, synthf) +
	            // the step-offset of this string based on "column"
	            stride(scale, maxi(0, param_eval_finger(P_STRIDE, fi, synthf)), fi) +
	            // the step-offset caused by the pad on the string
	            pad)
	    +
	    // add this to the pitch of the bottom-left pad
	    12
	        * (
	            // octave offset
	            (param_eval_finger(P_OCT, fi, synthf) << 9) +
	            // pitch offset
	            (param_eval_finger(P_PITCH, fi, synthf) >> 7));
}

int string_center_pitch(u8 fi) {
	return (string_pitch_at_pad(fi, 0) + string_pitch_at_pad(fi, 7)) / 2;
}

// find the string whose center pitch is the closest to midi_pitch
u8 find_string_for_midi_pitch(int midi_pitch) {
	// pitch falls below bottom string center
	if (midi_pitch < string_center_pitch(0))
		return 0;
	// pitch falls above top string center
	if (midi_pitch >= string_center_pitch(7))
		return 7;
	// find the string with the closest center pitch
	u8 desired_string = 0;
	int min_dist = 2147483647; // int max
	for (u8 i = 0; i < 8; i++) {
		int pitch_dist = abs(string_center_pitch(i) - midi_pitch);
		if (pitch_dist < min_dist) {
			min_dist = pitch_dist;
			desired_string = i;
		}
	}
	return desired_string;
}

int find_string_position_for_midi_pitch(u8 fi, int midi_pitch) {
	// note that string positions are ordered top-to-bottom
	static const u16 pad_spacing = 256;
	// return the position of the highest pad the midi pitch is higher than - or equal to
	for (u8 pad = 7; pad > 0; pad--) {
		if (midi_pitch >= string_pitch_at_pad(fi, pad)) {
			return (7 - pad) * pad_spacing;
		}
	}
	// if the pitch was lower than the pitch of pad 1, we return the bottom pad
	return 7 * pad_spacing;
}

u8 find_free_midi_string(u8 midi_note_number, int* midi_note_position) {
	Touch* synthf = touch_synth_getlatest(0);
	int midi_pitch =
	    // base pitch
	    12 * ((param_eval_finger(P_OCT, 0, synthf) << 9) + (param_eval_finger(P_PITCH, 0, synthf) >> 7)) +
	    // pitch
	    ((midi_note_number - 24) << 9);

	// find the best string for this midi note
	u8 desired_string = find_string_for_midi_pitch(midi_pitch);

	// try to find:
	// 1. the non-sounding string closest to our desired string
	// 2. the sounding string that is the quietest
	int string_option[8];
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
	for (uint8_t option_id = 0; option_id < num_string_options; option_id++) {
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
	for (uint8_t option_id = 0; option_id < num_string_options; option_id++) {
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

u8 pres_compress(int pressure) {
	return clampi((pressure + 12) / 24, 0, 255);
}
int pres_decompress(int pressure) {
	return maxi(randrange(24 * pressure - 12, 24 * pressure + 12), 0);
}

u8 pos_compress(int position) {
	return clampi((position + 4) / 8, 0, 255);
}
int pos_decompress(int position) {
	return maxi(randrange(8 * position - 4, 8 * position + 4), 0);
}

void finger_synth_update(int fi) {
	static u8 last_edited_step_global = 255;
	static u8 last_edited_substep_global = 255;
	static u8 last_edited_step[8] = {255, 255, 255, 255, 255, 255, 255, 255};
	static int record_to_substep;
	static bool suppress_latch = false;

	int bit = 1 << fi;
	bool is_read = touch_read_this_frame(fi);
	Touch* ui_finger = get_touch_prev(fi, is_read ? 0 : 1);
	Touch* synth_finger = &fingers_synth_time[fi][finger_frame_synth];
	int previous_pressure = get_touch_prev(fi, is_read ? 2 : 3)->pres;
	int substep = calcseqsubstep(0, 8);
	bool latchon = (rampreset.flags & FLAGS_LATCH);
	int pressure = 0;
	int position = 0;
	bool pressure_increasing;
	bool position_updated = false;

	// === TOUCH INPUT === //

	if (strip_available_for_synth(fi)) {
		// read touch values from ui
		pressure = ui_finger->pres;
		position = ui_finger->pos;
		pressure_increasing = (pressure > previous_pressure);
		if (pressure > 0) {
			physical_touch_finger |= bit;
			position_updated = true;
		}
		else
			physical_touch_finger &= ~bit;

		if (latchon) {

			// === LATCH WRITE === //

			// finger touching and pressure increasing
			if (pressure > 0 && pressure_increasing) {
				// is this a new touch after no fingers where touching?
				if (previous_pressure <= 0 && physical_touch_finger == bit) {
					// start a new latch, clear all previous latch values
					for (uint8_t i = 0; i < 8; i++) {
						latch[i].avgvel = 0;
						latch[i].minpos = 0;
						latch[i].maxpos = 0;
					}
					// in step record mode, trying to start a new latch temporarily turns off latching
					// trying to start a new latch outside of step record mode turns it on again
					suppress_latch = recording && !isplaying();
				}
				// save latch values
				if (!suppress_latch) {
					latch[fi].avgvel = pres_compress(pressure);
					latch[fi].minpos = pos_compress(position);
				}
				// RJ: I could not work out a way to work with average values that wasn't
				// sluggish or gave undesired intermediate values - slides and in-between notes
				// Current solution is just saving one value and randomizing when reading it out
				// Result feels great, but good to reconsider when the exact contents of
				// touchstrip and fingers_synth_time are more clear

				// Averaging code for reference:
				//
				// u8 maxpos = 0, minpos = 255, maxpressure = 0;
				// Touch* f = fingers_synth_time[fi];
				// for (int j = 0; j < 8; ++j, ++f) {
				// 	u8 p = clampi((f->pos + 4) / 8, 0, 255);
				// 	minpos = mini(p, minpos);
				// 	maxpos = maxi(p, maxpos);
				// 	u8 pr = clampi(f->pres / 12, 0, 255);
				// 	maxpressure = maxi(maxpressure, pr);
				// }
				// latch[fi].avgvel = maxpressure;
				// latch[fi].minpos = minpos;
				// latch[fi].maxpos = maxpos;
			}

			// === LATCH RECALL === //

			// latch pressure larger than touch pressure
			if (latch[fi].avgvel > 0 && latch[fi].avgvel * 24 > pressure) {
				// recall latch values
				pressure = pres_decompress(latch[fi].avgvel);
				;
				position = pos_decompress(latch[fi].minpos);
				position_updated = true;

				// Averaging code for reference:
				//
				// int minpos = latch[fi].minpos * 8 + 2;
				// int maxpos = latch[fi].maxpos * 8 + 6;
				// int avgpos = (minpos + maxpos) / 2;
				// int range = (maxpos - minpos) / 4;
				// pressure = latchpres ? randrange(latchpres - 12, latchpres) : -1024;
				// position = randrange(avgpos-range,avgpos+range);
			}
		}

		// === SEQ RECORDING === //

		// RJ: because of the way data is stored in the sequencer, it is currently not possible to record
		// midi data into it without imposing some serious restrictions on the range of midi notes that
		// can be played. So we're only doing this for touches for now

		int quarter = (cur_step >> 4) & 3;
		FingerRecord* seq_record = &rampattern[quarter].steps[cur_step & 15][fi];
		int data_saved = false;
		// We're recording into the loaded pattern
		if (recording && rampattern_idx == cur_pattern) {
			// holding clear sets the pressure to zero, which will effectively clear the sequencer at this point
			int seq_pressure = shift_state == SS_CLEAR ? 0 : pres_compress(pressure);
			int seq_position = shift_state == SS_CLEAR ? 0 : pos_compress(position);

			// holding a note or clearing during playback
			if ((seq_pressure > 0 && pressure_increasing) || shift_state == SS_CLEAR) {
				// live recording
				if (isplaying()) {
					record_to_substep = substep;
				}
				// step recording
				else {
					// editing a new step, and waited for substep to reset to zero
					if (cur_step != last_edited_step_global && substep == 0) {
						// we have not edited any substep
						last_edited_substep_global = 255;
						// we have not edited this step for any finger
						memset(last_edited_step, 255, sizeof(last_edited_step));
						// start at substep 0
						record_to_substep = 0;
						// this skips the first increment of record_to_substep, right below
						last_edited_substep_global = 0;
						// we're now editing the current step
						last_edited_step_global = cur_step;
					}
					// editing a new substep
					if (substep != last_edited_substep_global) {
						// are we in the step?
						if (record_to_substep < 8) {
							// move one substep forward
							record_to_substep++;
						}
						// are we at the end of the step?
						else {
							// push all data one substep backward
							for (u8 i = 0; i < 7; i++) {
								seq_record->pres[i] = seq_record->pres[i + 1];
								if (!(substep & 1) && !(i & 1))
									seq_record->pos[i / 2] = seq_record->pos[i / 2 + 1];
							}
						}
						last_edited_substep_global = substep;
					}
					// first finger edit on this step
					if (cur_step != last_edited_step[fi]) {
						// clear the step for this finger
						memset(seq_record->pres, 0, sizeof(seq_record->pres));
						memset(seq_record->pos, 0, sizeof(seq_record->pos));
						// we're now editing this step with this finger
						last_edited_step[fi] = cur_step;
					}
				}
				// record!
				seq_record->pres[mini(record_to_substep, 7)] = seq_pressure;
				seq_record->pos[mini(record_to_substep, 7) / 2] = seq_position;
				data_saved = true;
			}
			if (data_saved)
				ramtime[GEN_PAT0 + quarter] = millis();
		}
		// not recording
		else {
			// clear this for next recording
			last_edited_step_global = 255;
		}
	}

	// === SEQ PLAYING === //

	// no pressure generated by touch or latch and sequencer wants to play
	if (pressure <= 0 && (isplaying() || seq_rhythm.did_a_retrig)) {
		FingerRecord* seq_record = readpattern(fi);
		if (seq_record) {
			int gatelen = param_eval_finger(P_GATE_LENGTH, fi, synth_finger) >> 8;
			int small_substep = calcseqsubstep(0, 256);
			if (seq_record->pres[substep] && !seq_rhythm.supress && small_substep <= gatelen
			    && shift_state != SS_CLEAR) {
				read_from_seq = true;
				pressure = pres_decompress(seq_record->pres[substep]);
				position = pos_decompress(seq_record->pos[substep / 2]);
				position_updated = true;
			}
		}
	}

	// === MIDI INPUT === //

	// midi gets suppressed if touch, latch or sequencer generate pressure
	if (pressure > 0)
		midi_suppress |= bit;
	else
		midi_suppress &= ~bit;
	// a midi note is playing this string
	if ((midi_pressure_override & bit) && !(midi_suppress & bit)) {
		// take pressure and position from midi data
		pressure = 1 + (midi_velocities[fi] + maxi(midi_aftertouch[fi], midi_chan_aftertouch[midi_channels[fi]])) * 16;
		// for midi, position only defines where the leds light up on the string
		position = midi_positions[fi];
	}

	// manually save position for release phase
	if (position_updated) {
		memory_position[fi] = position;
	}

	// === CV GATE === //

	// scale pressure with cv gate input
	pressure = (int)((pressure + 256) * adc_get_smooth(ADC_S_GATE)) - 256;

	// save input results to global variables to be used by other code
	synth_finger->pres = pressure;
	synth_finger->pos = position;
	total_ui_pressure += maxi(0, synth_finger->pres);
	if (synth_finger->pres > 0) {
		synthfingerdown_nogatelen_internal |= bit;
	}
	else {
		synthfingerdown_nogatelen_internal &= ~bit;
	}

	// new finger touch, slightly randomize touch-values in history to avoid static touch values
	static s16 prevpressure[8];
	if (prevpressure[fi] <= 0 && synth_finger->pres > 0) {
		Touch* of = fingers_synth_time[fi];
		int newp = synth_finger->pos;
		for (int h = 0; h < 8; ++h, of++)
			if (h != finger_frame_synth) {
				if (of->pres <= 0) {
					of->pos = (of->pos & 3) ^ newp;
				}
			}
	}
	prevpressure[fi] = synth_finger->pres;

	// sort fingers by pitch
	sort8((int*)fingers_synth_sorted[fi], (int*)fingers_synth_time[fi]);
}
