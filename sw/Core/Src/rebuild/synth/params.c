#include "params.h"
#include "data/tables.h"
#include "gfx/data/icons.h"
#include "gfx/gfx.h"
#include "hardware/adc_dac.h"
#include "lfos.h"
#include "sequencer.h"
#include "strings.h"
#include "synth.h"
#include "time.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

// cleanup
extern int16_t accel_raw[3];
extern float accel_lpf[2];
extern float accel_smooth[2];
extern Preset rampreset;
extern PatternQuarter rampattern[NUM_QUARTERS];
extern u32 ramtime[GEN_LAST];
extern SysParams sysparams;
extern Preset const init_params;
bool CopyPresetToRam(bool force);
// -- cleanup

#define EDITING_PARAM (selected_param < NUM_PARAMS)

// only global for ui.h
Param selected_param = 255;
ModSource selected_mod_src = SRC_BASE;
Param mem_param = 255; // remembers previous selected_param, used by encoder and A/B shift-presses

// modulation values
static u16 max_env_global = 0;
static u16 max_pres_global = 0;
static u16 sample_hold_global = {8 << 12};
static u16 sample_hold_poly[NUM_STRINGS] = {0, 1 << 12, 2 << 12, 3 << 12, 4 << 12, 5 << 12, 6 << 12, 7 << 12};

// editing params
static bool param_from_mem = false;
static s16 left_strip_start = 0;
static ValueSmoother left_strip_smooth;

static Touch* touch_pointer[NUM_STRINGS];

// == INLINES == //

static int get_volume_as_param(void) {
	return (sysparams.headphonevol + 45) * (PARAM_SIZE / 64);
}

static void set_arp(bool on) {
	if (on == (rampreset.flags && FLAGS_ARP))
		return;
	if (on)
		rampreset.flags |= FLAGS_ARP;
	else
		rampreset.flags &= ~FLAGS_ARP;
	ShowMessage(F_32_BOLD, on ? "arp on" : "arp off", 0);
	ramtime[GEN_SYS] = millis();
}

static void toggle_arp(void) {
	rampreset.flags ^= FLAGS_ARP;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_ARP)) ? "arp on" : "arp off", 0);
	ramtime[GEN_SYS] = millis();
}

static void set_latch(bool on) {
	if (on == (rampreset.flags && FLAGS_LATCH))
		return;
	if (on)
		rampreset.flags |= FLAGS_LATCH;
	else
		rampreset.flags &= ~FLAGS_LATCH;
	ShowMessage(F_32_BOLD, on ? "latch on" : "latch off", 0);
	ramtime[GEN_SYS] = millis();
}

static void toggle_latch(void) {
	rampreset.flags ^= FLAGS_LATCH;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_LATCH)) ? "latch on" : "latch off", 0);
	ramtime[GEN_SYS] = millis();
	if (!((rampreset.flags & FLAGS_LATCH)))
		clear_latch();
}

// == HELPERS == //

Param get_recent_param(void) {
	return EDITING_PARAM ? selected_param : mem_param;
}

// will this strip produce a press for the synth?
bool strip_available_for_synth(u8 strip_id) {
	// yes, in the default ui
	if (ui_mode == UI_DEFAULT
	    // but not the left-most strip when a parameter is being edited
	    && !(strip_id == 0 && EDITING_PARAM))
		return true;
	// in all other modes and situations: no
	return false;
}

// to prevent redundant calls to get_string_touch(), we save our own list of pointers to the relevant Touch*
// elements every time strings_frame increments
void params_update_touch_pointers(void) {
	for (u8 string_id = 0; string_id < NUM_STRINGS; string_id++)
		touch_pointer[string_id] = get_string_touch(string_id);
}

// == MAIN == //

void params_tick(void) {

	// == envelope 2 == //

	apply_lfo_mods(P_ENV_LVL2);
	apply_lfo_mods(P_ATTACK2);
	apply_lfo_mods(P_DECAY2);
	apply_lfo_mods(P_SUSTAIN2);
	apply_lfo_mods(P_RELEASE2);

	max_pres_global = 0;
	max_env_global = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id) {
		// update envelope 2
		u8 mask = 1 << string_id;
		Voice* v = &voices[string_id];
		// reset envelope on new touch
		if (string_touch_start & mask) {
			v->env2_lvl = 0.f;
			v->env2_decaying = false;
		}
		bool touching = string_touched & mask;
		// set lvl_goal
		float lvl_goal = touching
		                     // touching the string
		                     ? (v->env2_decaying)
		                           // decay stage: 2 times sustain parameter
		                           ? 2.f * (param_val_poly(P_SUSTAIN2, string_id) * (1.f / 65536.f))
		                           // attack stage: we aim for 2.2, the actual peak is at 2.0
		                           : 2.2f
		                     // not touching, release stage: 0
		                     : 0.f;
		float lvl_diff = lvl_goal - v->env2_lvl;
		// get multiplier size (scaled exponentially)
		float k = lpf_k(param_val_poly((lvl_diff > 0.f)
		                                   // positive difference => moving up => attack param
		                                   ? P_ATTACK2
		                                   : (v->env2_decaying && touching)
		                                         // negative difference and decaying => decay param
		                                         ? P_DECAY2
		                                         // negative difference and not decaying => release param
		                                         : P_RELEASE2,
		                               string_id));
		// change env level by fraction of difference
		v->env2_lvl += lvl_diff * k;
		// if we went past the peak during the attack stage, start the decay stage
		if (v->env2_lvl >= 2.f && touching)
			v->env2_decaying = true;
		// scale the envelope from a roughly [0, 2] float, to a u16 range scaled by the envelope level parameter
		v->env2_lvl16 = SATURATE17(v->env2_lvl * param_val_poly(P_ENV_LVL2, string_id));

		// collect range pressure
		max_pres_global = maxi(max_pres_global, touch_pointer[string_id]->pres);
		// collect range envelope
		max_env_global = maxf(max_env_global, voices[string_id].env2_lvl16);
		// generate polyphonic sample & hold random value on new touch
		if (string_touch_start & mask)
			sample_hold_poly[string_id] += 4813;
	}
	// scale range pressure to u16 range
	max_pres_global *= 32;
	// generate global sample & hold random value on new touch
	if (string_touch_start)
		sample_hold_global += 4813;

	// accelerometer(clean up later)
	s16 accel_sens = clampi(param_val(P_ACCEL_SENS) / 2, -32767, 32767);
	static u16 accel_counter;
	float accel_sens_f = (2.f / 16384.f / 32768.f) * abs(accel_sens);
	// run 2 plinkys have the accelerometer rotated 90 degrees and upside touching from the addon, detect it via z
	// direction
	bool axis_swap = accel_raw[2] > 4000;
	for (u8 axis_id = 0; axis_id < 2; ++axis_id) {
		float f = accel_raw[axis_id ^ axis_swap] * accel_sens_f;
		if (!axis_id) {
			if (!axis_swap)
				f = -f; // reverse x
		}
		else if (accel_sens < 0)
			f = -f; // reverse y if accel sens negative
		accel_lpf[axis_id] += (f - accel_lpf[axis_id]) * 0.0001f;
		accel_smooth[axis_id] += (f - accel_smooth[axis_id]) * 0.1f;
		if (accel_counter < 1000) {
			accel_lpf[axis_id] = accel_smooth[axis_id] = f;
			accel_counter++;
		}
	}

	adc_update_inputs();

	update_lfos();

	// apply lfo modulation to al other params
	for (Param param_id = 0; param_id < NUM_PARAMS; ++param_id) {
		// we already did envelope 2 above
		if (param_id == P_ENV_LVL2) {
			param_id += 5;
			continue;
		}
		// we already did the lfos above
		if (param_id == P_A_SCALE) {
			param_id += 23;
			continue;
		}
		apply_lfo_mods(param_id);
	}
}

// == RETRIEVAL == //

// get the raw, unmodulated parameter from the preset
s16 param_val_raw(Param param_id, ModSource mod_src) {
	if (param_id == P_VOLUME)
		return mod_src == SRC_BASE ? get_volume_as_param() : 0;
	return rampreset.params[param_id][mod_src];
}

// rj: the only reason the unscaled functions exist is because of one single call in ui.h - rewriting that so this
// doesn't need a global unscaled function would allow this to look a lot cleaner
static s32 param_val_unscaled_local(Param param_id, u16 rnd, u16 env, u16 pres) {
	s16* param = rampreset.params[param_id];
	// param_with_lfo has already applied the four lfos
	s32 new_val = param_with_lfo[param_id];

	// apply sample & hold mod source
	if (param[SRC_RND]) {
		u16 rnd_id = (u16)(rnd + param_id);
		// positive => uniform distribution
		if (param[SRC_RND] > 0)
			new_val += (rndtab[rnd_id] * param[SRC_RND]) << 8;
		// negative => triangular distribution
		else {
			rnd_id += rnd_id;
			new_val += (((s32)rndtab[rnd_id] - (s32)rndtab[rnd_id - 1]) * param[SRC_RND]) << 8;
		}
	}

	// apply envelope mod source
	new_val += env * param[SRC_ENV2];

	// apply pressure mod source
	new_val += pres * param[SRC_PRES];

	// all 7 mod sources have now been applied, clamp value and return
	return clampi(new_val >> 10, (param_range[param_id] & RANGE_SIGNED) ? -65536 : 0,
	              (param_range[param_id] & RANGE_MASK) ? 65535 : 65536);
}

// get unscaled modulated parameter value
s32 param_val_unscaled(Param param_id) {
	return param_val_unscaled_local(param_id, sample_hold_global, max_env_global, max_pres_global);
}

// ranged params
static s32 param_val_local(Param param_id, u16 rnd, u16 env, u16 pres) {
	u8 flags = param_range[param_id];
	u8 range = flags & RANGE_MASK;
	int new_val = param_val_unscaled_local(param_id, rnd, env, pres);
	if (range)
		new_val = (new_val * range) >> 16;
	return new_val;
}

// modulated and scaled parameter value
s32 param_val(Param param_id) {
	return param_val_local(param_id, sample_hold_global, max_env_global, max_pres_global);
}

// modulated and scaled parameter value in float format, range [-1.0, 1.0)
float param_val_float(Param param_id) {
	return param_val(param_id) * (1.f / 65536.f);
}

// modulated and scaled parameter value, polyphonic
s32 param_val_poly(Param param_id, u8 string_id) {
	// string pressure is scaled to u16 range before passing on
	return param_val_local(param_id, sample_hold_poly[string_id], voices[string_id].env2_lvl16,
	                       maxi(touch_pointer[string_id]->pres, 0) * 32);
}

// == SAVING == //

void save_param_raw(Param param_id, ModSource mod_src, s16 data) {
	// invalid request
	if (param_id >= NUM_PARAMS || mod_src >= NUM_MOD_SOURCES)
		return;
	// special headphone case
	if (param_id == P_VOLUME) {
		if (mod_src == SRC_BASE) {
			data = clampi(-45, ((data + (PARAM_SIZE / 128)) / (PARAM_SIZE / 64)) - 45, 18);
			if (data == sysparams.headphonevol)
				return;
			sysparams.headphonevol = data;
			ramtime[GEN_SYS] = millis();
		}
		return;
	}
	// edit gets discarded if previous change isn't saved yet
	if (!CopyPresetToRam(false))
		return;
	// don't save if no change
	s16 old_data = param_val_raw(param_id, mod_src);
	if (old_data == data)
		return;
	// save
	rampreset.params[param_id][mod_src] = data;
	apply_lfo_mods(param_id);
	ramtime[GEN_PRESET] = millis();
}

// constrain data to param_range bounds before saving, all data is scaled to s8 or smaller
void save_param(Param param_id, ModSource mod_src, s16 data) {
	u8 range = param_range[param_id] & RANGE_MASK;
	// ranged parameters are truncated and scaled to [0, PARAM_SIZE] or [-PARAM_SIZE, PARAM_SIZE]
	if (range > 0) {
		data %= range;
		if (data < 0 && !(param_range[param_id] & RANGE_SIGNED))
			data += range;
		data = ((data * 2 + 1) * PARAM_SIZE) / (range * 2);
	}
	save_param_raw(param_id, mod_src, data);
}

// == PAD ACTION == //

void try_left_strip_for_params(u16 position, bool is_press_start) {
	// only if editing a parameter
	if (!EDITING_PARAM)
		return;

	// scale the press position to a param size value
	float press_value = clampf((2048 - 256 - position) * (PARAM_SIZE / (2048.f - 512.f)), 0.f, PARAM_SIZE);
	bool is_signed = (param_range[selected_param] & RANGE_SIGNED) || (selected_mod_src != SRC_BASE);
	if (is_signed)
		press_value = press_value * 2 - PARAM_SIZE;
	// smooth the pressed value
	smooth_value(&left_strip_smooth, press_value, PARAM_SIZE);
	float smoothed_value = clampf(left_strip_smooth.y2, (is_signed) ? -PARAM_SIZE - 0.1f : 0.f, PARAM_SIZE + 0.1f);
	// value stops exactly at zero when crossing it
	if (smoothed_value < 0.f && left_strip_start > 0)
		smoothed_value = 0.f;
	if (smoothed_value > 0.f && left_strip_start < 0)
		smoothed_value = 0.f;
	// value stops exactly halfway when crossing center
	bool notch_at_50 = (selected_param == P_SMP_SPEED || selected_param == P_SMP_STRETCH);
	if (notch_at_50) {
		if (smoothed_value < HALF_PARAM_SIZE && left_strip_start > HALF_PARAM_SIZE)
			smoothed_value = HALF_PARAM_SIZE;
		if (smoothed_value > HALF_PARAM_SIZE && left_strip_start < HALF_PARAM_SIZE)
			smoothed_value = HALF_PARAM_SIZE;
		if (smoothed_value < -HALF_PARAM_SIZE && left_strip_start > -HALF_PARAM_SIZE)
			smoothed_value = -HALF_PARAM_SIZE;
		if (smoothed_value > -HALF_PARAM_SIZE && left_strip_start < -HALF_PARAM_SIZE)
			smoothed_value = -HALF_PARAM_SIZE;
	}
	// save the value to the parameter
	save_param_raw(selected_param, selected_mod_src, (s16)smoothed_value);
}

// returns whether this activated a different param
bool press_param(u8 pad_y, u8 strip_id, bool is_press_start) {
	// pressing a parameter always reverts to the "base" mod src
	selected_mod_src = SRC_BASE;
	// select param based on pressed pad
	u8 prev_param = selected_param;
	selected_param = pad_y * 12 + (strip_id - 1) + (ui_mode == UI_EDITING_B ? 6 : 0);

	// largely show the new param on screen when it changes
	if (EDITING_PARAM && selected_param != prev_param)
		gfx_flash_parameter(selected_param);

	// parameters that do something the moment they are pressed
	if (is_press_start) {
		switch (selected_param) {
		case P_ARP_TOGGLE:
			toggle_arp();
			break;
		case P_LATCH_TOGGLE:
			toggle_latch();
			break;
		case P_TEMPO:
			trigger_tap_tempo();
			break;
		default:
			break;
		}
	}

	return selected_param != prev_param;
}

void select_mod_src(ModSource mod_src) {
	selected_mod_src = mod_src;
}

void reset_left_strip(void) {
	left_strip_start = param_val_raw(selected_param, selected_mod_src);
	set_smoother(&left_strip_smooth, left_strip_start);
}

// == SHIFT STATE == //

void enter_param_edit_mode(bool mode_a) {
	// remember parameter
	if (!EDITING_PARAM && mem_param < NUM_PARAMS) {
		selected_param = mem_param;
		param_from_mem = true;
	}
	else
		param_from_mem = false;
	// pick the correct A/B parameter on this pad
	if (EDITING_PARAM && ((selected_param % 12 < 6) ^ mode_a))
		selected_param += mode_a ? -6 : 6;
}

// this gets triggered when an A / B shift state pad gets released
void try_exit_param_edit_mode(bool param_select) {
	// arp & latch are fake params => exit on shift state release and don't remember the param
	if (selected_param == P_ARP_TOGGLE || selected_param == P_LATCH_TOGGLE) {
		selected_param = NUM_PARAMS;
		selected_mod_src = 0;
		return;
	}
	// we don't exit if a parameter was retrieved from memory when we entered edit mode
	if (param_from_mem)
		return;
	// we don't exit if during edit mode, a parameter was selected
	if (param_select)
		return;
	// otherwise this was a press-and-release while a param was showing => exit and remember the param
	mem_param = selected_param;
	selected_param = NUM_PARAMS;
	selected_mod_src = 0;
}

// == ENCODER == //

void edit_param_from_encoder(Param param_id, s8 enc_diff, float enc_acc) {
	// retrieve parameters
	s16 cur_val = param_val_raw(param_id, selected_mod_src);
	s16 new_val = cur_val;
	u8 range = param_range[param_id];
	// mod sources are always signed
	bool is_signed = (range & RANGE_SIGNED) || selected_mod_src;

	// base values of params that have a constrained range change with 1 per encoder-notch (non-scaled)
	if ((range & RANGE_MASK) && (selected_mod_src == SRC_BASE)) {
		u8 maxi = range & RANGE_MASK;
		new_val += enc_diff * (PARAM_SIZE / maxi);
	}
	// for all other values, the encoder input is scaled by an acceleration value
	else {
		u8 enc_sens = (param_id == P_VOLUME) ? 4 : 1;
		new_val += (s16)floorf(0.5f + enc_diff * enc_sens * maxf(1.f, enc_acc * enc_acc));
	}
	new_val = clampi(new_val, is_signed ? -PARAM_SIZE : 0, PARAM_SIZE);
	// save new_val if it has changed
	if (cur_val != new_val) {
		save_param_raw(param_id, selected_mod_src, new_val);
	}
}

// rj: it looks like the intention here was to toggle between the saved value and the default value, but in reality the
// first press switches from the saved value to the default value, which from that point toggles between the default
// value and zero, or between 100% and zero
void params_toggle_default_value(Param param_id) {
	s16 saved_val = param_val_raw(param_id, selected_mod_src);
	s16 init_val = selected_mod_src ? 0 : init_params.params[param_id][0];
	s16 new_val = saved_val;
	if (init_val != 0) {
		if (new_val != init_val)
			new_val = init_val;
		else
			new_val = 0;
	}
	else {
		if (new_val != 0)
			new_val = 0;
		else
			new_val = PARAM_SIZE;
	}
	if (new_val != saved_val)
		save_param_raw(param_id, selected_mod_src, new_val);
}

void hold_encoder_for_params(u16 duration) {
	if (!EDITING_PARAM)
		return;
	if (duration == 50)
		for (ModSource mod_src = SRC_ENV2; mod_src < NUM_MOD_SOURCES; ++mod_src)
			save_param_raw(selected_param, mod_src, 0);
	if (duration >= 50)
		ShowMessage(F_20_BOLD, I_CROSS "Mod Cleared", "");
	else if (duration >= 10)
		ShowMessage(F_20_BOLD, I_CROSS "Clear Mod?", "");
}

void check_param_toggles(Param param_id) {
	if (param_id == P_ARP_TOGGLE)
		toggle_arp();
	else if (param_id == P_LATCH_TOGGLE)
		toggle_latch();
}

// == MIDI == //

void set_param_from_cc(Param param_id, u16 value) {
	// toggles
	if (param_id == P_ARP_TOGGLE) {
		set_arp((bool)(value & (1 << 14)));
		return;
	}
	if (param_id == P_LATCH_TOGGLE) {
		set_latch((bool)(value & (1 << 14)));
		return;
	}
	// scale from 14 bit to PARAM_SIZE
	value = (value * PARAM_SIZE) / 16383;
	// scale from unsigned to signed
	if (param_range[param_id] & RANGE_SIGNED)
		value = value * 2 - PARAM_SIZE;
	// save
	save_param_raw(param_id, SRC_BASE, value);
}