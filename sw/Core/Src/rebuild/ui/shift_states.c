#include "shift_states.h"
#include "hardware/adc_dac.h"
#include "hardware/touchstrips.h"
#include "pad_actions.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "synth/time.h"
#include "ui.h"

// all of these need cleaning up
extern SysParams sysparams;       // system
extern u32 ramtime[GEN_LAST];     // system
extern u8 copy_request;           // system
extern s8 selected_preset_global; // system
// - all of these need cleaning up

#define SHORT_PRESS_TIME 250 // ms

ShiftState shift_state = SS_NONE;
bool action_pressed_during_shift = false;

static u8 prev_ui_mode = UI_DEFAULT;
static u32 shift_last_press_time = 0;
static bool param_from_mem = false;

// we'd prefer not exposing these
u32 shift_state_frames = 0;
bool shift_short_pressed(void) {
	return (shift_state == SS_NONE) || ((synth_tick - shift_last_press_time) < SHORT_PRESS_TIME);
}

void shift_set_state(ShiftState new_state) {
	shift_state = new_state;
	shift_last_press_time = synth_tick;
	shift_state_frames = 0;

	if (ui_mode == UI_SAMPLE_EDIT) {
		// record/play buttons have identical behavior
		if (new_state == SS_RECORD || new_state == SS_PLAY) {
			switch (sampler_mode) {
			// pre-armed moves to armed
			case SM_PRE_ARMED:
				sampler_mode = SM_ARMED;
				break;
			// armed starts recoding
			case SM_ARMED:
				start_recording_sample();
				break;
			// recording stops recording
			case SM_RECORDING:
				stop_recording_sample();
				break;
			default:
				break;
			}
		}
		return;
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
	action_pressed_during_shift = false;
	switch (shift_state) {
	case SS_SHIFT_A:
		// remember parameter
		if (!VALID_PARAM_SELECTED && last_selected_param < P_LAST) {
			selected_param = last_selected_param;
			param_from_mem = true;
		}
		else
			param_from_mem = false;
		// make sure our parameter is an A parameter
		if (VALID_PARAM_SELECTED && (selected_param % 12) >= 6)
			selected_param -= 6;
		// activate A edit mode
		ui_mode = UI_EDITING_A;
		break;
	case SS_SHIFT_B:
		// identical to SS_SHIFT_A
		if (!VALID_PARAM_SELECTED && last_selected_param < P_LAST) {
			selected_param = last_selected_param;
			param_from_mem = true;
		}
		else
			param_from_mem = false;
		if (VALID_PARAM_SELECTED && (selected_param % 12) < 6)
			selected_param += 6;
		ui_mode = UI_EDITING_B;
		break;
	case SS_LOAD:
		// activate preset load screen
		ui_mode = UI_LOAD;
		selected_preset_global = sysparams.curpreset;
		break;
	case SS_LEFT:
		// if playing, jump to start of pattern
		if (seq_playing()) {
			seq_jump_to_start();
			// resync if we're using the internal clock
			if (using_internal_clock)
				seq_resync();
		}
		// edit start of sequencer pattern
		ui_mode = UI_PTN_START;
		break;
	case SS_RIGHT:
		// activate set pattern end screen
		ui_mode = UI_PTN_END;
		break;
	case SS_CLEAR:
		// pressing Clear stops latched notes playing
		clear_latch();
		break;
	case SS_PLAY:
		if (seq_flags.stop_at_next_step)
			// cued to stop? => stop immediately
			seq_stop();
		else if (seq_flags.playing)
			// playing but not cued to stop? => cue to stop
			seq_cue_to_stop();
		else
			// not playing? => initiate preview
			seq_start_previewing();
		break;
	default:
		break;
	}
}

void shift_release_state(void) {
	bool short_press = shift_short_pressed();
	shift_state_frames = 0;

	if (ui_mode == UI_SAMPLE_EDIT) {
		// short presses in sample edit mode
		if (short_press)
			switch (shift_state) {
			case SS_SHIFT_A:
				sampler_toggle_play_mode();
				break;
			case SS_SHIFT_B:
				sampler_iterate_loop_mode();
				break;
			case SS_LOAD:
			case SS_LEFT:
			case SS_RIGHT:
			case SS_CLEAR:
				// middle four buttons => general canceling command
				switch (sampler_mode) {
				case SM_PREVIEW:
					// when in default (preview) mode => exit sampler
					ui_mode = UI_DEFAULT;
					break;
				case SM_RECORDING:
					// when recording => stop recording
					stop_recording_sample();
					break;
				default:
					// when in any of the other sampler modes => move to default (preview) mode
					sampler_mode = SM_PREVIEW;
					break;
				}
				break;
			default:
				break;
			}
		// we're no longer in a shift state
		shift_state = SS_NONE;
		return; // exit
	}

	// all other modes
	switch (shift_state) {
	case SS_SHIFT_A:
	case SS_SHIFT_B:
		// return to default mode
		ui_mode = UI_DEFAULT;
		last_selected_param = selected_param; // save param
		if (!action_pressed_during_shift && !param_from_mem) {
			selected_param = P_LAST;
			selected_mod_src = 0;
		}
		// these arent real params, so don't remember them
		if (selected_param == P_ARPONOFF || selected_param == P_LATCHONOFF) {
			selected_param = P_LAST;
			selected_mod_src = 0;
		}
		break;
	case SS_LOAD:
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_LEFT:
		if (!action_pressed_during_shift && short_press) {
			// a short press left only steps left in the sequencer when it's not playing
			if (!seq_playing()) {
				seq_dec_step();
				seq_force_play_step();
				// resync if we're using the internal clock
				if (using_internal_clock)
					seq_resync();
			}
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_RIGHT:
		// short press right steps the sequencer right one step
		if (!action_pressed_during_shift && short_press) {
			seq_inc_step();
			// resync if we're using the internal clock
			if (using_internal_clock)
				seq_resync();
			// sound it out if we're not playing
			if (!seq_playing())
				seq_force_play_step();
			ui_mode = UI_DEFAULT;
		}
		if (action_pressed_during_shift || prev_ui_mode == ui_mode)
			ui_mode = UI_DEFAULT;
		break;
	case SS_CLEAR:
		// pressing clear in step-record mode clears sequencer step
		if (ui_mode == UI_DEFAULT && seq_state() == SEQ_STEP_RECORDING) {
			seq_clear_step();
			// move to next step after clearing
			seq_inc_step();
		}
		break;
	case SS_RECORD:
		if (short_press)
			seq_toggle_rec();
		break;
	case SS_PLAY:
		// - a short press ends previewing and resumes playing normally
		// - a long press means this is the end of a preview and we should stop playing
		if (seq_flags.previewing)
			short_press ? seq_end_previewing() : seq_stop();
		break;
	default:
		break;
	}

	// we're no longer in a shift state
	shift_state = SS_NONE;
}

void shift_hold_state(void) {
	switch (ui_mode) {
	case UI_LOAD:
		// after a delay, initalize preset
		if ((shift_state == SS_CLEAR) && (shift_state_frames > 64 + 4) && (selected_preset_global >= 0)
		    && (selected_preset_global < 64))
			copy_request = selected_preset_global + 128;
		break;
	case UI_SAMPLE_EDIT:
		// long-pressing record or play in sampler preview mode records a new sample
		if (sampler_mode == SM_PREVIEW && (shift_state == SS_RECORD || shift_state == SS_PLAY)
		    && shift_state_frames > 64)
			start_erasing_sample_buffer();
		break;
	default:
		break;
	}

	// increase hold duration
	shift_state_frames++;
}
