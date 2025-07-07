#include "synth.h"
#include "hardware/adc_dac.h"
#include "hardware/cv.h"
#include "hardware/midi.h"
#include "hardware/midi_defs.h"
#include "hardware/touchstrips.h"
#include "pitch_tools.h"
#include "strings.h"

// cleanup
void RunVoice(Voice* v, int fingeridx, u32* outbuf);
int param_eval_int(u8 paramidx, int rnd, int env16, int pressure16);
float table_interp(const float* table, int x);
extern Voice voices[8];
extern s8 arpoctave;
extern s8 arpmode;
extern u8 arpbits;
extern u16 any_rnd;
extern int env16;
extern int pressure16;
// -- cleanup

static bool got_high_pitch = false; // did we get a high pitch?
static s32 cv_gate_value;           // cv gate value
static bool got_low_pitch = false;  // did we get a low pitch?
static s32 strings_low_pitch = 0;   // pitch on lowest string seen
bool cv_trig_high = false;          // should cv trigger be high?
s32 strings_high_pitch = 0;         // pitch on highest string seen
u16 strings_max_pressure = 0;       // highest pressure seen

// initialize oscillator generation
static void init_strings_for_osc_generation(void) {
	// clear variables
	cv_trig_high = false;
	got_high_pitch = false;
	cv_gate_value = 0;
	got_low_pitch = false;
	strings_max_pressure = 0;

	// some pre-calculations
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++) {
		u8 mask = 1 << string_id;
		// midi note is playing its release phase and has rung out
		if ((midi_pitch_override & mask) && !(midi_pressure_override & mask) && !(midi_suppress & mask)
		    && (voices[string_id].vol < 0.001f))
			// disable pitch override, this truly turns off the note
			midi_pitch_override &= ~mask;
		// max pres for all strings
		Touch* s_touch = get_string_touch(string_id);
		if (s_touch->pres > strings_max_pressure)
			strings_max_pressure = s_touch->pres;
	}
}

// take string values, calculate osc pitches and write them to voice
static void generate_oscs_from_string(u8 string_id, Voice* voice) {
	Touch* s_touch = get_string_touch(string_id);
	float pres_scaled = s_touch->pres * 1.f / TOUCH_MAX_POS;

	// rj: cv_gate_value is in practice another expression of the maximum pressure over all strings, which goes against
	// eurorack conventions (gates are generally high/low, 5V/0V) and I'm also not sure what the added value of this is,
	// since we already have an expression of the max pressure on the pressure CV out - should we make gate out binary?
	cv_gate_value = maxi(cv_gate_value, (int)(pres_scaled * 65536.f));

	// only recalculate the oscillator pitches if the string is touched
	if (pres_scaled < 0.001f)
		return;

	u8 mask = 1 << string_id;
	s32 note_pitch = 0;   // pitch offset caused by the played note
	s32 fine_pitch = 0;   // pitch offset caused by micro_tone / spread
	s32 osc_pitch = 0;    // resulting pitch of current oscillator
	s32 summed_pitch = 0; // summed pitch of four oscillators

	// these only get used by touch
	Scale scale = S_LAST;
	s32 cv_pitch_offset = 0;
	s8 cv_step_offset = 0;

	// saving string values that are the same for all oscillators

	s32 base_pitch = 12 *
	                 // pitch from arp and the octave parameter
	                 (((arpoctave + param_eval_finger(P_OCT, string_id, s_touch)) << 9)
	                  // pitch from the pitch parameter
	                  + (param_eval_finger(P_PITCH, string_id, s_touch) >> 7));
	// pitch from interval parameter
	s32 osc_interval_pitch = (param_eval_finger(P_INTERVAL, string_id, s_touch) * 12) >> 7;
	// scale step from rotate parameter
	s8 string_step_offset = param_eval_finger(P_ROTATE, string_id, s_touch);

	// for midi
	if ((midi_pitch_override & mask) && !(midi_suppress & mask)) {
		note_pitch = midi_note_to_pitch_offset(midi_note[string_id], midi_channel[string_id]);
	}
	// for touch
	else {
		// this param retrieval should be simplified in params
		scale = param_eval_finger(P_SCALE, string_id, s_touch);
		if (scale >= S_LAST)
			scale = 0;

		// add step offset caused by scale & column (stride) parameters
		string_step_offset += scale_steps_at_string(scale, string_id, s_touch);

		// cv
		s32 cv_pitch = adc_get_smooth(ADC_S_PITCH);
		if (param_eval_int(P_CV_QUANT, any_rnd, env16, pressure16) == CVQ_SCALE)
			cv_step_offset = pitch_to_scale_steps(cv_pitch, scale); // quantized cv
		else
			cv_pitch_offset = cv_pitch; // unquantized cv
	}

	// we discard the two highest and lowest positions and use elements 2 through 5 to generate our oscillator pitches
	Touch* s_touch_sort = sorted_string_touch_ptr(string_id) + 2;

	// loop through oscillators
	for (u8 osc_id = 0; osc_id < NUM_OSCILLATORS; ++osc_id) {
		// for midi
		if ((midi_pitch_override & mask) && !(midi_suppress & mask)) {
			// generate pitch spread
			fine_pitch = (osc_id - 2) * 64;
		}
		// for touch
		else {
			u16 position = s_touch_sort->pos; // touch position
			u8 pad_y = 7 - (position >> 8);   // pad on string
			// pitch at step + cv
			note_pitch = pitch_at_step(scale, string_step_offset + pad_y + cv_step_offset) + cv_pitch_offset;
			// detuning scaled by microtune param
			s16 fine_pos = 127 - (position & 255); // offset from pad center
			s32 micro_tune = 64 + param_eval_finger(P_MICROTUNE, string_id, s_touch);
			fine_pitch = (fine_pos * micro_tune) >> 14;
		}

		// calculate resulting pitch
		osc_pitch =
		    // octave and pitch parameters
		    base_pitch +
		    // pitch from scale step / midi note
		    note_pitch +
		    // pitch from osc interval parameter
		    ((osc_id & 1) ? osc_interval_pitch : 0) +
		    // pitch from micro_tone / pitch spread
		    fine_pitch;

		// save values
		summed_pitch += osc_pitch;
		voice->theosc[osc_id].pitch = osc_pitch;
		voice->theosc[osc_id].targetdphase =
		    maxi(65536, (s32)(table_interp(pitches, osc_pitch + PITCH_BASE) * (65536.f * 128.f)));
		++s_touch_sort;
	}

	// these are saving respectively the lowest and highest string that are pressed, which is not necessarily
	// always the lowest/highest pitch played
	if (!got_low_pitch) {
		strings_low_pitch = summed_pitch;
		got_low_pitch = true;
	}
	if (arpmode < 0 || (arpbits & (1 << string_id))) {
		strings_high_pitch = summed_pitch;
		got_high_pitch = true;
	}
	// the outgoing midi note is generated from oscillator pitch
	set_midi_goal_note(string_id, quad_pitch_to_midi_note(summed_pitch));
}

// send cv values resulting from oscillator generation
static void send_cvs_from_strings(void) {
	send_cv_trigger(cv_trig_high);
	if (got_high_pitch)
		send_cv_pitch_hi(strings_high_pitch, true);
	send_cv_gate(mini(cv_gate_value, 65535));
	if (got_low_pitch)
		send_cv_pitch_lo(strings_low_pitch, true);
	send_cv_pressure(strings_max_pressure * 8);
}

void handle_synth_voices(u32* dst) {
	init_strings_for_osc_generation();
	for (u8 voice_id = 0; voice_id < NUM_VOICES; ++voice_id) {
		generate_oscs_from_string(voice_id, &voices[voice_id]);
		RunVoice(&voices[voice_id], voice_id, dst);
	}
	send_cvs_from_strings();
}