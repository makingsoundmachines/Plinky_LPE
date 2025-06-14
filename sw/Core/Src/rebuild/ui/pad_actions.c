#include "pad_actions.h"
#include "gfx/data/names.h"
#include "hardware/touchstrips.h"
#include "shift_states.h"
#include "ui.h"

// needs cleaning up

// system
extern u32 ramtime[GEN_LAST];
extern Preset rampreset;
extern u32 tick;
extern SysParams sysparams;
extern s8 selected_preset_global; // system
void knobsmooth_reset(ValueSmoother* s, float ival);
float smooth_value(ValueSmoother* s, float newval, float max_scale);
void SetPreset(u8 preset, bool force);
// gfx
#include "gfx/gfx.h"
void ShowMessage(Font fnt, const char* msg, const char* submsg);
// sampler
extern s8 enable_audio;
extern u8 recsliceidx;
extern u8 cur_sample1;
extern u8 edit_sample0;
extern SampleInfo ramsample;
extern u8 ramsample1_idx;
extern u8 pending_sample1;
extern u8 copy_request;
void init_slice_edit(SampleInfo* s, int strip_id);
void EditParamQuant(u8 paramidx, u8 mod, s16 data);
SampleInfo* GetSavedSampleInfo(u8 sample0);
SampleInfo* getrecsample(void);
// time
extern int tap_count;
extern s32 bpm10x;
// sequencer
extern u8 pending_loopstart_step;
extern s8 cur_step;
extern u8 cur_pattern;
extern s8 step_offset;
void recording_trigger(void);
void recording_stop(void);
bool isplaying(void);
void set_cur_step(u8 newcurstep, bool triggerit);
// parameters
extern u8 preset_copy_source;
extern u8 pattern_copy_source;
extern u8 sample_copy_source;
extern u8 pending_preset;
extern u8 pending_pattern;
extern u8 pending_sample1;
extern u8 prev_pending_preset;
extern u8 prev_pending_pattern;
extern u8 prev_pending_sample1;
int GetParam(u8 paramidx, u8 mod);
void EditParamQuant(u8 paramidx, u8 mod, s16 data);
void EditParamNoQuant(u8 paramidx, u8 mod, s16 data);
void toggle_latch(void);
void toggle_arp(void);
// - needs cleaning up

#define LONGPRESS_THRESH 160 // full read cycles

u8 selected_param = 255;
u8 selected_mod_src = M_BASE;

// needed by drawing ui.h, should be local
u8 last_selected_param = 255;
u8 strip_holds_valid_action = 0; // mask
u8 strip_is_action_pressed = 0;  // mask
u16 long_press_frames = 0;
u8 long_press_pad = 0;

static float strip_start_pos[8];
static int saved_param_value[8];
static ValueSmoother param_value_smoother[8];

static u8 get_load_section(u8 pad_id) {
	return pad_id < 32   ? 0  // presets
	       : pad_id < 56 ? 1  // patterns
	       : pad_id < 64 ? 2  // samples
	                     : 3; // none
}

void init_slice_edit(SampleInfo* s, int strip_id) { // this belongs in sampler
	recsliceidx = strip_id;
	saved_param_value[strip_id] = s->splitpoints[strip_id];
	knobsmooth_reset(&param_value_smoother[strip_id], saved_param_value[strip_id]);
}

// will this strip produce a press for the synth?
bool strip_available_for_synth(u8 strip_id) {
	// yes, in the default ui
	if (ui_mode == UI_DEFAULT
	    // but not the left-most strip when a parameter is being edited
	    && !(strip_id == 0 && VALID_PARAM_SELECTED))
		return true;
	// in all other modes and situations: no
	return false;
}

void handle_pad_actions(u8 strip_id, Touch* strip_cur) {
	Touch* strip_2back = get_touch_prev(strip_id, 2);
	u8 strip_mask = 1 << strip_id;
	u8 pad_y = strip_cur->pos >> 8;   // local pad (on strip, 0 - 7)
	u8 pad_id = strip_id * 8 + pad_y; // global pad (on plate, 0 - 63)

	// == pad presses == //

	bool pres_stable = abs(strip_2back->pres - strip_cur->pres) < 200;
	bool pos_stable = abs(strip_2back->pos - strip_cur->pos) < 32;
	bool is_press_start = false;

	//  touch + pressure over 100
	if (strip_cur->pres > 100) {
		if (!strip_available_for_synth(strip_id))
			strip_holds_valid_action |= strip_mask; // set valid action flag
		// we're stable
		if (pos_stable && pres_stable) {
			if (!strip_is_action_pressed)
				long_press_pad = pad_id; // first press gets tracked as long press
			if (!(strip_is_action_pressed & strip_mask)) {
				is_press_start = true;
				strip_is_action_pressed |= strip_mask; // set pressed flag
				strip_start_pos[strip_id] = strip_cur->pos;
			}
		}
	}

	else {                                                                    // pressure under 100,
		strip_holds_valid_action &= ~strip_mask;                              // clear valid action flag
		if ((strip_is_action_pressed & strip_mask) && strip_cur->pres <= 0) { // pressure under 0,
			strip_is_action_pressed &= ~strip_mask;                           // clear pressed flag
			// release in load preset mode
			if (ui_mode == UI_LOAD) {
				s8 load_section = get_load_section(long_press_pad);
				switch (load_section) {
				case 0: // presets
					if (pending_preset == prev_pending_preset || !isplaying()) {
						if (pending_preset != 255) {
							SetPreset(pending_preset, false);
							pending_preset = 255;
						}
					}
					break;
				case 1: // patterns
					if (pending_pattern == prev_pending_pattern || !isplaying()) {
						if (pending_pattern != 255) {
							EditParamQuant(P_SEQPAT, M_BASE, pending_pattern);
							pending_pattern = 255;
						}
					}
					break;
				case 2: // samples
					if (pending_sample1 == prev_pending_sample1 || !isplaying()) {
						if (pending_sample1 != 255) {
							EditParamQuant(P_SAMPLE, M_BASE, pending_sample1);
							pending_sample1 = 255;
						}
					}
					break;
				}
			}
		}
	}

	// == executing actions == //

	if ((strip_is_action_pressed & strip_mask) && (strip_holds_valid_action & strip_mask)) {
		action_pressed_during_shift = true;

		// these belong in sequencer
		u8 ptn_step = pad_y * 8 + strip_id;
		u8 ptn_start_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;

		switch (ui_mode) {
		case UI_DEFAULT:
		case UI_EDITING_A:
		case UI_EDITING_B: {
			u8 prev_selected_param = selected_param;
			// selected a parameter (center six strips)
			if (strip_id > 0 && strip_id < 7) {
				// show the user the base value
				selected_mod_src = M_BASE;
				selected_param = pad_y * 12 + strip_id - 1;
				if (ui_mode == UI_EDITING_B) // offset for b params
					selected_param += 6;
				// newly selected params are displayed on screen, largely, before reverting to the param edit screen
				if (VALID_PARAM_SELECTED && selected_param != prev_selected_param)
					ShowMessage(F_20_BOLD, paramnames[selected_param], pagenames[selected_param / 6]);

				// parameters that do something the moment they are pressed
				if (is_press_start) {
					// fake the arp and latch toggles
					if (selected_param == P_ARPONOFF)
						toggle_arp();
					else if (selected_param == P_LATCHONOFF)
						toggle_latch();

					// tap tempo! (this should live in time module)
					if (selected_param == P_TEMPO) {
						static u32 start_tap;
						static u32 last_tap;
						if (tick - last_tap > 1000) // tap tempo can't be slower than 1 sec beats
							tap_count = 0;
						if (!tap_count)
							start_tap = tick;
						last_tap = tick;
						tap_count++;
						if (tap_count > 1) {
							float taps_per_minute =
							    (32000.f * (tap_count - 1) * 60.f) / ((tick - start_tap) * BLOCK_SAMPLES);
							bpm10x = clampi((int)(taps_per_minute * 10.f + 0.5f), 300, 2400);
							EditParamNoQuant(P_TEMPO, 0, ((bpm10x - 1200) * FULL) / 1200);
						}
					}
				}
			}
			// new valid param press
			if (VALID_PARAM_SELECTED && (selected_param != prev_selected_param || is_press_start)) {
				// set the value smoother to the value of that param
				saved_param_value[strip_id] = GetParam(selected_param, selected_mod_src);
				knobsmooth_reset(&param_value_smoother[strip_id], saved_param_value[strip_id]);
			}
			// new valid mod source press
			if (strip_id == 7 && pad_y != selected_mod_src) {
				// select it
				selected_mod_src = pad_y;
			}
			// left-most strip used to edit param value
			if (strip_id == 0 && VALID_PARAM_SELECTED && pres_stable) {
				// position of the press
				float press_value = clampf((2048 - 256 - strip_cur->pos) * (FULL / (2048.f - 512.f)), 0.f, FULL);
				bool is_signed = (param_flags[selected_param] & FLAG_SIGNED) | (selected_mod_src != M_BASE);
				if (is_signed)
					press_value = press_value * 2 - FULL;
				// smooth the pressed value
				float smoothed_value = smooth_value(&param_value_smoother[strip_id], press_value, FULL);
				smoothed_value = clampf(smoothed_value, (is_signed) ? -FULL - 0.1f : 0.f, FULL + 0.1f);
				// value stops exactly at zero when crossing it
				if (smoothed_value < 0.f && saved_param_value[strip_id] > 0)
					smoothed_value = 0.f;
				if (smoothed_value > 0.f && saved_param_value[strip_id] < 0)
					smoothed_value = 0.f;
				// value stops exactly halfway when crossing center
				bool notch_at_50 = (selected_param == P_SMP_RATE || selected_param == P_SMP_TIME);
				if (notch_at_50) {
					if (smoothed_value < HALF && saved_param_value[strip_id] > HALF)
						smoothed_value = HALF;
					if (smoothed_value > HALF && saved_param_value[strip_id] < HALF)
						smoothed_value = HALF;
					if (smoothed_value < -HALF && saved_param_value[strip_id] > -HALF)
						smoothed_value = -HALF;
					if (smoothed_value > -HALF && saved_param_value[strip_id] < -HALF)
						smoothed_value = -HALF;
				}
				// save the value to the parameter
				EditParamNoQuant(selected_param, selected_mod_src, (s16)smoothed_value);
			}
		} break;
		case UI_SAMPLE_EDIT: { // this belongs in the sampler
			SampleInfo* s = getrecsample();
			if (shift_state == SS_NONE && is_press_start) {
				if (enable_audio == EA_MONITOR_LEVEL) {
					enable_audio = EA_ARMED;
				}
				// any press while armed starts the recording
				else if (enable_audio == EA_ARMED) {
					recording_trigger();
				}
				// a press while recording, and the recorded chunk is at least a block
				else if (enable_audio == EA_RECORDING && s->samplelen >= BLOCK_SAMPLES) {
					if (recsliceidx < 7) { // add slice points if there is still room
						recsliceidx++;
						s->splitpoints[recsliceidx] = s->samplelen - BLOCK_SAMPLES;
					}
					else { // when all slices are filled, a press ends the recording
						recording_stop();
					}
				}
			}
			// long press during recording stops the recording
			if (enable_audio == EA_RECORDING && s->samplelen >= BLOCK_SAMPLES && long_press_frames > 32) {
				if (recsliceidx > 0)
					recsliceidx--;
				recording_stop();
			}
			// in play mode, a touch adjusts the slice point
			if (enable_audio == EA_PLAY) {
				if (is_press_start) {
					init_slice_edit(s, strip_id);
				}
				float pos_offset = (strip_cur->pos - strip_start_pos[strip_id]);
				pos_offset = deadzone(pos_offset, 32.f);
				float press_value = saved_param_value[strip_id] - pos_offset * (32000.f / 2048.f);
				float smoothed_value = smooth_value(&param_value_smoother[strip_id], press_value, 32000.f);
				float smin = strip_id ? s->splitpoints[strip_id - 1] + 1024.f : 0.f;
				float smax = (strip_id < 7) ? s->splitpoints[strip_id + 1] - 1024.f : s->samplelen;
				if (smin < 0.f)
					smin = 0.f;
				if (smax > s->samplelen)
					smax = s->samplelen;
				smoothed_value = clampf(smoothed_value, smin, smax);
				if (s->splitpoints[strip_id] != smoothed_value) {
					s->splitpoints[strip_id] = smoothed_value;
					ramtime[GEN_SAMPLE] = millis();
				}
			}
		} break;
		case UI_PTN_START: // (this should live in sequencer)
			// press inside of the pattern
			if (((ptn_step - ptn_start_step) & 63) < rampreset.looplen_step) {
				// set current pattern step to pressed step
				if (is_press_start)
					set_cur_step(ptn_step, false);
			}
			// press outside of the pattern
			else {
				// we were about to move to this step or we're not playing: set loop start and move step immediately
				if ((is_press_start && pending_loopstart_step == ptn_step) || !isplaying()) {
					if (ptn_start_step != ptn_step) {
						u8 newstep = cur_step - ptn_start_step + ptn_step; // move curstep into new loop
						ptn_start_step = ptn_step;
						rampreset.loopstart_step_no_offset = (ptn_step - step_offset) & 63;
						ramtime[GEN_PRESET] = millis();
						set_cur_step(newstep, false); // reset our cur ptn_step, based on the new loop
					}
					pending_loopstart_step = 255;
				}
				// otherwise, tell sequencer to set the first step on the next end-of-step
				else
					pending_loopstart_step = ptn_step;
			}
			break;
		case UI_PTN_END: // (this should live in sequencer)
			// set the end of the loop (always immediately)
			{
				u8 prev_step = rampreset.looplen_step;
				rampreset.looplen_step = (ptn_step - ptn_start_step) + 1;
				if (rampreset.looplen_step <= 0)
					rampreset.looplen_step += 64;
				if (rampreset.looplen_step != prev_step)
					ramtime[GEN_PRESET] = millis(); // only write if changed
				set_cur_step(cur_step, false);      // reset our cur ptn_step, based on the new loop
			}
			break;
		case UI_LOAD: { // belongs in load/save module
			selected_preset_global = pad_id;
			s8 load_section = get_load_section(pad_id);
			if (load_section == get_load_section(long_press_pad))
				// line up items to be set (*_pending) or copied from (copy_source)
				switch (load_section) {
				case 0:
					// preset
					preset_copy_source = sysparams.curpreset;
					prev_pending_preset = pending_preset;
					pending_preset = pad_id;
					break;
				case 1: {
					// pattern
					pattern_copy_source = cur_pattern;
					prev_pending_pattern = pending_pattern;
					pending_pattern = pad_id - 32;
					break;
				}
				case 2: {
					// sample
					int sample_id = pad_id - 56 + 1;
					if (is_press_start) {
						prev_pending_sample1 = pending_sample1;
						sample_copy_source = cur_sample1;
						if (sample_id == sample_copy_source)
							sample_id = 0;
						pending_sample1 = sample_id;
					}
					break;
				}
				} // section switch
			break;
		} // preset
		} // mode
	}
}

void handle_pad_action_long_presses(void) {
	// long-press is only used in load and sample edit modes
	if (ui_mode != UI_LOAD && ui_mode != UI_SAMPLE_EDIT)
		return;
	// we're looking for exactly one strip being pressed for an action
	if (!strip_is_action_pressed || !ispow2(strip_is_action_pressed)) {
		long_press_frames = 0;
		return;
	}
	// find the single pressed strip
	u8 strip_id = 0;
	for (; strip_id < 8; ++strip_id)
		if (strip_is_action_pressed & (1 << strip_id))
			break;
	// get the pressed pad
	u8 pad_id = 8 * strip_id + (get_touch_prev(strip_id, 1)->pos >> 8);
	// only relevant if the pressed pad is the long_press_pad
	if (pad_id != long_press_pad) {
		long_press_frames = 0;
		return;
	}
	// increase counter
	long_press_frames += 2;
	// actions on long press
	if (ui_mode == UI_LOAD && long_press_frames == LONGPRESS_THRESH) {
		// sample pad (strip 7), load sample and enter sample edit mode (belongs in sampler)
		if (strip_id == 7) {
			edit_sample0 = (pad_id & 7);
			EditParamQuant(P_SAMPLE, 0, edit_sample0 + 1);
			memcpy(&ramsample, GetSavedSampleInfo(edit_sample0), sizeof(SampleInfo));
			ramsample1_idx = cur_sample1 = edit_sample0 + 1;
			pending_sample1 = 255;
			ui_mode = UI_SAMPLE_EDIT;
			init_slice_edit(&ramsample, 7);
		}
		// patch or pattern, request copy
		else
			copy_request = pad_id;
	}
}