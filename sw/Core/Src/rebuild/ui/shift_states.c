#include "shift_states.h"
#include "hardware/touchstrips.h"
#include "ui.h"

// all of these need cleaning up
extern SysParams sysparams;          // system
extern u32 ramtime[GEN_LAST];        // system
extern u8 copy_request;              // system
extern u32 record_flashaddr_base;    // system
extern knobsmoother adc_smooth[8];   // ADC
extern float knobbase[2];            // potentiometers
extern bool got_ui_reset;            // timing
extern u32 tick;                     // timing
extern int tap_count;                // timing
extern s8 enable_audio;              // audio
extern u8 playmode;                  // sequencer
extern u8 recording_knobs;           // sequencer
extern s8 cur_step;                  // sequencer
extern bool recording;               // sequencer
extern PatternQuarter rampattern[4]; // sequencer
extern SampleInfo ramsample;         // sampler
extern knobsmoother recgain_smooth;  // sampler
extern int audiorec_gain_target;     // sampler
extern u8 edit_sample0;              // sampler
extern u8 recsliceidx;               // sampler
extern int recstartpos;              // sampler
extern int recreadpos;               // sampler
extern int recpos;                   // sampler
extern bool pre_erase;               // sampler

// all sequencer from here
extern void recording_trigger(void);
extern void recording_stop(void);
extern void OnLoop(void);
extern void seq_step(int initial);
extern bool isplaying(void);
extern void clear_latch(void);
extern void set_cur_step(u8 newcurstep, bool triggerit);
extern void check_curstep(void);
extern void knobsmooth_reset(knobsmoother* s, float ival);
// - all of these need cleaning up

#define SHORT_PRESS_TIME 250 // ms

ShiftState shift_state = SS_NONE;

static u8 prev_ui_mode = UI_DEFAULT;
static u32 shift_last_press_time = 0;
static bool param_from_mem = false;

// we'd prefer not exposing these
u32 shift_state_frames = 0;
bool shift_short_pressed(void) {
	return (shift_state == SS_NONE) || ((tick - shift_last_press_time) < SHORT_PRESS_TIME);
}

void shift_set_state(ShiftState new_state) {
	shift_state = new_state;
	shift_last_press_time = tick;
	shift_state_frames = 0;

	// sample edit mode
	if (ui_mode == UI_SAMPLE_EDIT) {
		// record/play buttons have identical behavior
		if (shift_state == SS_RECORD || shift_state == SS_PLAY) {
			switch (enable_audio) {
			// once arms the recording
			case EA_MONITOR_LEVEL:
				enable_audio = EA_ARMED;
				break;
			// twice starts the recording
			case EA_ARMED:
				recording_trigger();
				break;
			// thrice stops the recording
			case EA_RECORDING:
				recording_stop();
				break;
			}
		}
		return; // done
	}

	// == Overview of the system == //
	//
	// Shift A / Shift B:
	// - pressing shift pad sets ui_mode to UI_EDITING_A / UI_EDITING_B
	// - releasing shift pad immediately reverts to UI_DEFAULT
	// - the state of touched_main_area and param_from_mem decide whether a parameter stays selected for editing, or
	// gets cleared, when the shift pad is released
	//
	// Preset / Left / Right
	// - ui_mode UI_LOAD / UI_PTN_START / UI_PTN_END gets set when pad is pressed
	// - mode gets reverted to UI_DEFAULT on release of the pad, depending on these circumstances:
	// 		- a non-shift pad was pressed while the shift button was held ("quick edit", shift + pad)
	//		- no change in ui_mode happened when the shift pad was pressed (the pad was pressed while in its own mode)
	// - Left/Right also revert to UI_DEFAULT at the end of a short press, as that indicates a sequencer step action
	//
	// Clear / Record / Play
	// - do not change ui_mode at all
	// - press and release actions each have their own sequencer-related actions

	// all other modes
	prev_ui_mode = ui_mode;
	touched_main_area = false;
	switch (shift_state) {
	case SS_SHIFT_A:
		// remember parameter
		if (edit_param >= P_LAST && last_edit_param < P_LAST) {
			edit_param = last_edit_param;
			param_from_mem = true;
		}
		else
			param_from_mem = false;
		// make sure our parameter is an A parameter
		if (edit_param < P_LAST && (edit_param % 12) >= 6)
			edit_param -= 6;
		// activate A edit mode
		ui_mode = UI_EDITING_A;
		tap_count = 0; // move to time module (tap tempo)
		break;
	case SS_SHIFT_B:
		// identical to SS_SHIFT_A (no tap tempo)
		if (edit_param >= P_LAST && last_edit_param < P_LAST) {
			edit_param = last_edit_param;
			param_from_mem = true;
		}
		else
			param_from_mem = false;
		if (edit_param < P_LAST && (edit_param % 12) < 6)
			edit_param += 6;
		ui_mode = UI_EDITING_B;
		break;
	case SS_LOAD:
		// activate preset load screen
		ui_mode = UI_LOAD;
		last_preset_selection_rotstep = sysparams.curpreset;
		break;
	case SS_LEFT:
		// activate set pattern start screen
		ui_mode = UI_PTN_START;
		// this is a timing/sequencer thing, needs to move there
		if (isplaying())
			got_ui_reset = true;
		break;
	case SS_RIGHT:
		// activate set pattern end screen
		ui_mode = UI_PTN_END;
		break;
	case SS_CLEAR:
		// pressing Clear stops latched notes playing
		clear_latch();
		break;
	case SS_RECORD:
		// this belongs in the sequencer
		// also recording_knobs is currently not implented
		knobbase[0] = adc_smooth[4].y2;
		knobbase[1] = adc_smooth[5].y2;
		recording_knobs = 0;
		break;
	case SS_PLAY:
		// this logic should live in the sequencer
		switch (playmode) {
		case PLAY_STOPPED:
			playmode = PLAY_PREVIEW;
			seq_step(1);
			break;
		case PLAYING:
			playmode = PLAY_WAITING_FOR_CLOCK_STOP;
			break;
		case PLAY_WAITING_FOR_CLOCK_STOP:
			playmode = PLAY_STOPPED;
			OnLoop();
			break;
		}
		break;
	default:
		break;
	}
}

void shift_release_state(void) {
	bool short_press = shift_short_pressed();
	shift_state_frames = 0;

	// sample edit mode
	if (ui_mode == UI_SAMPLE_EDIT) {
		if (short_press) {
			if (shift_state == SS_SHIFT_A) { // toggle sample pitch/tape modes
				ramsample.pitched = !ramsample.pitched;
				ramtime[GEN_SAMPLE] = millis();
			}
			else if (shift_state == SS_SHIFT_B) { // iterate through sample loop modes
				ramsample.loop = (ramsample.loop + 1) & 3;
				ramtime[GEN_SAMPLE] = millis();
			}
			else if (shift_state != SS_RECORD && shift_state != SS_PLAY) { // middle four buttons stop the recording
				recording_stop();
			}
		}
		shift_state = SS_NONE; // we're no longer in a shift state
		return;                // done
	}

	// all other modes
	switch (shift_state) {
	case SS_SHIFT_A:
	case SS_SHIFT_B:
		// return to default mode
		ui_mode = UI_DEFAULT;
		last_edit_param = edit_param; // save param
		if (!touched_main_area && !param_from_mem) {
			edit_param = P_LAST;
			edit_mod = 0;
		}
		// these arent real params, so don't remember them
		if (edit_param == P_ARPONOFF || edit_param == P_LATCHONOFF) {
			edit_param = P_LAST;
			edit_mod = 0;
		}
		break;
	case SS_LOAD:
		if (touched_main_area || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_LEFT:
		if (!touched_main_area && short_press) {
			// this belongs in sequencer
			if (!isplaying())
				set_cur_step(cur_step - 1, !isplaying()); // short left press moves one step back
			// return to default mode
			ui_mode = UI_DEFAULT;
		}
		if (touched_main_area || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_RIGHT:
		if (!touched_main_area && short_press) {
			// this belongs in sequencer
			set_cur_step(cur_step + 1, !isplaying()); // short right press moves one step forward
			// return to default mode
			ui_mode = UI_DEFAULT;
		}
		if (touched_main_area || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_CLEAR:
		if (!isplaying() && recording && ui_mode == UI_DEFAULT) { // clear step record mode seq step
			bool dirty = false;
			int q = (cur_step >> 4) & 3;
			FingerRecord* strip_right = &rampattern[q].steps[cur_step & 15][0];
			for (int fi = 0; fi < 8; ++fi, ++strip_right) {
				for (int k = 0; k < 8; ++k) {
					if (strip_right->pres[k] > 0)
						dirty = true;
					strip_right->pres[k] = 0;
					if (fi < 2) {
						s8* d = &rampattern[q].autoknob[(cur_step & 15) * 8 + k][fi];
						if (*d) {
							*d = 0;
							dirty = true;
						}
					}
				}
			}
			if (dirty)
				ramtime[GEN_PAT0 + ((cur_step >> 4) & 3)] = millis();
			set_cur_step(cur_step + 1, false); // and move next step
		}
		break;
	case SS_RECORD:
		// this belongs in sequencer
		if (short_press && recording_knobs == 0)
			recording = !recording; // short press toggles recording
		recording_knobs = 0;
		break;
	case SS_PLAY:
		// this belongs in sequencer
		if (playmode == PLAY_PREVIEW)
			// short pres plays the sequencer, long press is a preview and stops after release
			playmode = short_press ? PLAYING : PLAY_STOPPED;
		break;
	default:
		break;
	}
	// this belongs in sequencer
	check_curstep();

	// we're no longer in a shift state
	shift_state = SS_NONE;
}

void shift_hold_state(void) {
	switch (ui_mode) {
	case UI_SAMPLE_EDIT:
		// definitely some sampling stuff that belongs in the sampler
		if (enable_audio == EA_PLAY && (shift_state == SS_RECORD || shift_state == SS_PLAY)
		    && shift_state_frames > 64) {
			knobsmooth_reset(&recgain_smooth, audiorec_gain_target);
			record_flashaddr_base = (edit_sample0 & 7) * (2 * MAX_SAMPLE_LEN);
			recsliceidx = 0;
			recstartpos = 0;
			recreadpos = 0;
			recpos = 0;
			enable_audio = pre_erase ? EA_PREERASE : EA_MONITOR_LEVEL;
		}
		break;
	case UI_LOAD:
		// after a delay, initalize preset
		if ((shift_state == SS_CLEAR) && (shift_state_frames > 64 + 4) && (last_preset_selection_rotstep >= 0)
		    && (last_preset_selection_rotstep < 64))
			copy_request = last_preset_selection_rotstep + 128;
		break;
	default:
		break;
	}

	// increase hold duration
	shift_state_frames++;
}
