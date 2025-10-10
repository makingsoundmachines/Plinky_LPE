#include "params.h"
#include "data/tables.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/encoder.h"
#include "hardware/leds.h"
#include "hardware/ram.h"
#include "lfos.h"
#include "param_defs.h"
#include "sequencer.h"
#include "strings.h"
#include "synth.h"
#include "time.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"

#define EDITING_PARAM (selected_param < NUM_PARAMS)

// There are three ranges of parameters:
// Raw:
//  - saved parameters
//  - s16 in range -1024 to 1024
// Value:
//  - high resolution
//  - used for granular control such as envelope parameters
//  - are displayed on screen as -100.0 to 100.0
//  - s32 in range -65536 to 65536 (effectively raw << 6)
// Index:
//  - whole numbers
//  - used for discrete values such as octave offset and sequencer clock division
//  - s8 scaled and clamped to their own range
//  - mapping to index follows a simple (value * index_range / full_range) formula
//  - mapping from index will snap to the value closest to 0 that truncates to the given index:
//    index (range = 3)   |........-2|........-1|.........0.........|1.........|2.........|
//    raw            -1024|......-683|......-342|.........0.........|342.......|683.......|1024

static Param selected_param = 255;
static ModSource selected_mod_src = SRC_BASE;

// stable snapshots for drawing oled and led visuals
static Param param_snap;
static ModSource src_snap;

// modulation values
static s32 param_with_lfo[NUM_PARAMS];
static u16 max_env_global = 0;
static u16 max_pres_global = 0;
static u16 sample_hold_global = {8 << 12};
static u16 sample_hold_poly[NUM_STRINGS] = {0, 1 << 12, 2 << 12, 3 << 12, 4 << 12, 5 << 12, 6 << 12, 7 << 12};

// editing params
static Param mem_param = 255; // remembers previous selected_param, used by encoder and A/B shift-presses
static bool param_from_mem = false;
static s16 left_strip_start = 0;
static ValueSmoother left_strip_smooth;

static Touch* touch_pointer[NUM_STRINGS];

// == INLINES == //

static s32 SATURATE17(s32 a) {
	int tmp;
	asm("ssat %0, %1, %2" : "=r"(tmp) : "I"(17), "r"(a));
	return tmp;
}

static s8 value_to_index(s32 value, u8 range) {
	return (clampi(value, -65535, 65535) * range + (value < 0 ? 65535 : 0)) >> 16;
}

static s8 raw_to_index(s16 raw, u8 range) {
	return value_to_index(raw << 6, range);
}

static u8 param_range(Param param_id) {
	return param_info[range_type[param_id]] & RANGE_MASK;
}

bool param_signed(Param param_id) {
	return param_info[range_type[param_id]] & SIGNED;
}

static bool param_signed_or_mod(Param param_id, ModSource mod_src) {
	return param_signed(param_id) || mod_src != SRC_BASE;
}

// == HELPERS == //

const Preset* init_params_ptr() {
	return &init_params;
}

static Param get_recent_param(void) {
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

// is the arp actively being executed?
bool arp_active(void) {
	return param_index(P_ARP_TGL) && ui_mode != UI_SAMPLE_EDIT && seq_state() != SEQ_STEP_RECORDING;
}

// == MAIN == //

static void apply_lfo_mods(Param param_id) {
	s16* param = cur_preset.params[param_id];
	s32 new_val = param[SRC_BASE] << 16;
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++)
		new_val += lfo_cur[lfo_id] * param[SRC_LFO_A + lfo_id];
	param_with_lfo[param_id] = new_val;
}

void params_tick(void) {
	// envelope 2
	for (Param param_id = P_ENV_LVL2; param_id <= P_RELEASE2; param_id++)
		apply_lfo_mods(param_id);
	max_pres_global = 0;
	max_env_global = 0;
	for (u8 string_id = 0; string_id < NUM_STRINGS; ++string_id) {
		// update envelope 2
		u8 mask = 1 << string_id;
		Voice* v = &voices[string_id];
		// reset envelope on new touch
		if (env_trig_mask & mask) {
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
		if (env_trig_mask & mask)
			sample_hold_poly[string_id] += 4813;
	}
	// scale range pressure to u16 range
	max_pres_global *= 32;
	// generate global sample & hold random value on new touch
	if (env_trig_mask)
		sample_hold_global += 4813;

	accel_tick();

	adc_update_inputs();

	// lfos
	update_lfo_scope();
	for (u8 lfo_id = 0; lfo_id < NUM_LFOS; lfo_id++) {
		u8 lfo_row_offset = lfo_id * 6;
		// apply lfo modulation to the parameters of the lfo itself
		for (Param param_id = P_A_SCALE; param_id <= P_A_SYM; param_id++)
			apply_lfo_mods(param_id + lfo_row_offset);
		update_lfo(lfo_id);
	}

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

// raw parameter value, range -1024 to 1024
static s16 param_val_raw(Param param_id, ModSource mod_src) {
	if (param_id == P_VOLUME && mod_src == SRC_BASE)
		return sys_params.headphonevol << 2;
	return cur_preset.params[param_id][mod_src];
}

// modulated parameter value, range -65536 to 65536
static s32 param_val_mod(Param param_id, u16 rnd, u16 env, u16 pres) {
	s16* param = cur_preset.params[param_id];

	// pre-modulated with lfos, has 16 precision bits
	s32 mod_val = param_with_lfo[param_id];

	// apply envelope modulation
	mod_val += env * param[SRC_ENV2];

	// apply pressure modulation
	mod_val += pres * param[SRC_PRES];

	// apply sample & hold modulation
	if (param[SRC_RND]) {
		u16 rnd_id = (u16)(rnd + param_id);
		// positive => uniform distribution
		if (param[SRC_RND] > 0)
			mod_val += (rndtab[rnd_id] * param[SRC_RND]) << 8;
		// negative => triangular distribution
		else {
			rnd_id += rnd_id;
			mod_val += ((rndtab[rnd_id] - rndtab[rnd_id - 1]) * param[SRC_RND]) << 8;
		}
	}

	// all 7 mod sources have now been applied, scale and clamp to 16 bit
	return clampi(mod_val >> 10, param_signed(param_id) ? -65536 : 0, 65536);
}

// param value range +/- 65536

s32 param_val(Param param_id) {
	return param_val_mod(param_id, sample_hold_global, max_env_global, max_pres_global);
}

s32 param_val_poly(Param param_id, u8 string_id) {
	return param_val_mod(param_id, sample_hold_poly[string_id], voices[string_id].env2_lvl16,
	                     clampi(touch_pointer[string_id]->pres << 5, 0, 65535));
}

// index value is scaled to its appropriate range

s8 param_index(Param param_id) {
	s8 index = value_to_index(param_val(param_id), param_range(param_id));
	// special cases
	switch (range_type[param_id]) {
	// return the number of 32nds
	case R_CLOCK1:
	case R_CLOCK2:
		if (index >= 0)
			index = sync_divs_32nds[index];
		break;
	// revert from being stored 1-based
	case R_SAMPLE:
		index = (index - 1 + SAMPLE_ID_RANGE) % SAMPLE_ID_RANGE;
		break;
	default:
		break;
	}
	return index;
}

s8 param_index_poly(Param param_id, u8 string_id) {
	return value_to_index(param_val_poly(param_id, string_id), param_range(param_id));
}

// == SAVING == //

void save_param_raw(Param param_id, ModSource mod_src, s16 data) {
	// special cases
	switch (param_id) {
	case P_VOLUME:
		data = clampi(data >> 2, 0, 255);
		if (data == sys_params.headphonevol)
			return;
		sys_params.headphonevol = data;
		log_ram_edit(SEG_SYS);
		return;
	case P_LATCH_TGL:
		if (data >> 9 == 0)
			clear_latch();
		break;
	default:
		break;
	}
	// don't save if no change
	if (data == cur_preset.params[param_id][mod_src])
		return;
	// don't save if ram not ready
	if (!update_preset_ram(false))
		return;
	// save
	cur_preset.params[param_id][mod_src] = data;
	apply_lfo_mods(param_id);
	log_ram_edit(SEG_PRESET);
}

void save_param_index(Param param_id, s8 index) {
	// save 1-based
	if (param_id == P_SAMPLE)
		index = (index + 1) % SAMPLE_ID_RANGE;
	u8 range = param_range(param_id);
	index = clampi(index, param_signed(param_id) ? -(range - 1) : 0, range - 1);
	save_param_raw(param_id, SRC_BASE, INDEX_TO_RAW(index, range));
}

// == PAD ACTION == //

void try_left_strip_for_params(u16 position, bool is_press_start) {
	static const u16 STRIP_DEADZONE = 256;
	// only if editing a parameter
	if (!EDITING_PARAM)
		return;

	// scale the press position to a param size value
	float press_value =
	    clampf((TOUCH_MAX_POS - STRIP_DEADZONE - position) * (RAW_SIZE / (TOUCH_MAX_POS - 2.f * STRIP_DEADZONE)), 0.f,
	           RAW_SIZE);
	bool is_signed = param_signed_or_mod(selected_param, selected_mod_src);
	if (is_signed)
		press_value = press_value * 2 - RAW_SIZE;
	// smooth the pressed value
	smooth_value(&left_strip_smooth, press_value, 256);
	float smoothed_value = clampf(left_strip_smooth.y2, (is_signed) ? -RAW_SIZE - 0.1f : 0.f, RAW_SIZE + 0.1f);
	// value stops exactly halfway when crossing center
	bool notch_at_50 = (selected_param == P_PLAY_SPD || selected_param == P_SMP_STRETCH);
	if (notch_at_50) {
		if (smoothed_value < RAW_HALF && left_strip_start > RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value > RAW_HALF && left_strip_start < RAW_HALF)
			smoothed_value = RAW_HALF;
		if (smoothed_value < -RAW_HALF && left_strip_start > -RAW_HALF)
			smoothed_value = -RAW_HALF;
		if (smoothed_value > -RAW_HALF && left_strip_start < -RAW_HALF)
			smoothed_value = -RAW_HALF;
	}
	// save the value to the parameter
	save_param_raw(selected_param, selected_mod_src, (s16)(smoothed_value + (smoothed_value > 0 ? 0.5f : -0.5f)));
}

// returns whether this activated a different param
bool press_param(u8 pad_y, u8 strip_id, bool is_press_start) {
	// pressing a parameter always reverts to the "base" mod src
	selected_mod_src = SRC_BASE;
	// select param based on pressed pad
	u8 prev_param = selected_param;
	selected_param = pad_y * 12 + (strip_id - 1) + (ui_mode == UI_EDITING_B ? 6 : 0);
	// parameters that do something the moment they are pressed
	if (is_press_start) {
		// toggle binary params
		if (range_type[selected_param] == R_BINARY)
			save_param_index(selected_param, !(cur_preset.params[selected_param][SRC_BASE] >= RAW_HALF));
		if (selected_param == P_TEMPO)
			trigger_tap_tempo();
	}

	return selected_param != prev_param;
}

void select_mod_src(ModSource mod_src) {
	switch (selected_param) {
	case P_MIDI_CH_IN:
	case P_MIDI_CH_OUT:
	case P_VOLUME:
		flash_message(F_20_BOLD, "No Modulation", "");
		return;
	default:
		break;
	}
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

void edit_param_from_encoder(s8 enc_diff, float enc_acc) {
	Param param_id = get_recent_param();
	if (param_id >= NUM_PARAMS)
		return;

	// if this is a precision-edit, keep the param selected
	if (shift_state == SS_SHIFT_A || shift_state == SS_SHIFT_B)
		param_from_mem = true;

	s16 raw = param_val_raw(param_id, selected_mod_src);
	u8 range = param_range(param_id);

	// special case
	if (range_type[param_id] == R_CLOCK2 && raw < 0)
		range = 0;

	// indeces: just add/subtract 1 per encoder tick
	if (range && selected_mod_src == SRC_BASE) {
		s16 index = raw_to_index(raw, range);

		// retrieve 1-based
		if (param_id == P_SAMPLE)
			index = (index - 1 + SAMPLE_ID_RANGE) % SAMPLE_ID_RANGE;

		index += enc_diff;

		// smooth transition between synced and free timing
		if (range_type[param_id] == R_CLOCK2 && index < 0) {
			save_param_raw(param_id, SRC_BASE, -1);
			return;
		}

		save_param_index(param_id, index);
		return;
	}

	// full range values, add/subtract 0.1 per encoder tick with acceleration
	if (param_id == P_VOLUME)
		enc_diff *= 4;
	// holding shift disables acceleration
	enc_acc = shift_state == SS_SHIFT_A || shift_state == SS_SHIFT_B ? 1.f : maxf(1.f, enc_acc * enc_acc);
	raw += floorf(enc_diff * enc_acc + 0.5f);
	// make encoder steps of size 0.1 map to 1024 parameter values exactly
	s8 pos = raw & 127;
	if (pos == 1 || pos == 43 || pos == 86 || pos == -42 || pos == -85 || pos == -127)
		raw += enc_diff > 0 ? 1 : -1;
	raw = clampi(raw, param_signed_or_mod(param_id, selected_mod_src) ? -RAW_SIZE : 0, RAW_SIZE);
	save_param_raw(param_id, selected_mod_src, raw);
}

void params_toggle_default_value(void) {
	static u16 param_hash = NUM_PARAMS * NUM_MOD_SOURCES;
	static s16 saved_val = INT16_MAX;

	Param param_id = get_recent_param();
	if (param_id >= NUM_PARAMS)
		return;

	// clear saved value when we're seeing a new parameter
	u16 new_hash = param_id * NUM_MOD_SOURCES + selected_mod_src;
	if (new_hash != param_hash) {
		saved_val = INT16_MAX;
		param_hash = new_hash;
	}

	s16 cur_val = param_val_raw(param_id, selected_mod_src);
	s16 init_val = selected_mod_src ? 0 : init_params.params[param_id][0];
	// first press: save current value and set init value
	if (cur_val != init_val || saved_val == INT16_MAX) {
		saved_val = param_val_raw(param_id, selected_mod_src);
		save_param_raw(param_id, selected_mod_src, init_val);
	}
	// second press: restore saved value
	else
		save_param_raw(param_id, selected_mod_src, saved_val);
}

// == VISUALS == //

void hold_encoder_for_params(u16 duration) {
	if (!EDITING_PARAM)
		return;
	if (duration == 50)
		for (ModSource mod_src = SRC_ENV2; mod_src < NUM_MOD_SOURCES; ++mod_src)
			save_param_raw(selected_param, mod_src, 0);
	if (duration >= 50)
		flash_message(F_20_BOLD, I_CROSS "Mod Cleared", "");
	else if (duration >= 10)
		flash_message(F_20_BOLD, I_CROSS "Clear Mod?", "");
}

// == VISUALS == //

static const char* get_param_str(int p, int mod, int v, char* val_buf, char* dec_buf) {
	if (dec_buf)
		*dec_buf = 0;
	int valmax = param_range(p);
	int vscale = valmax ? (mini(v, RAW_SIZE - 1) * valmax) / RAW_SIZE : v;
	int displaymax = valmax ? valmax * 10 : 1000;
	bool decimal = true;
	//	const char* val = val_buf;
	if (mod == SRC_BASE)
		switch (p) {
		case P_SMP_STRETCH:
		case P_PLAY_SPD:
			displaymax = 2000;
			break;
		case P_ARP_TGL:
			return param_index(P_ARP_TGL) ? "On" : "Off";
		case P_LATCH_TGL:
			return param_index(P_LATCH_TGL) ? "On" : "Off";
		case P_SAMPLE:
			if (vscale == 0) {
				return "Off";
			}
			break;
		case P_SEQ_CLK_DIV: {
			if (vscale >= NUM_SYNC_DIVS)
				return "(Gate CV)";
			int n = sprintf(val_buf, "%d", sync_divs_32nds[vscale] /* >> divisor*/);
			if (!dec_buf)
				dec_buf = val_buf + n;
			sprintf(dec_buf, /*divisornames[divisor]*/ "/32");
			return val_buf;
		}
		case P_ARP_CLK_DIV:
		case P_A_RATE:
		case P_B_RATE:
		case P_X_RATE:
		case P_Y_RATE:
			if (v < 0) {
				v = -v;
				decimal = true;
				valmax = 0;
				displaymax = 1000;
				break;
			}
			vscale = (mini(v, RAW_SIZE - 1) * NUM_SYNC_DIVS) / RAW_SIZE;
			int n = sprintf(val_buf, "%d", sync_divs_32nds[vscale] /*>> divisor*/);
			if (!dec_buf)
				dec_buf = val_buf + n;
			sprintf(dec_buf, /*divisornames[divisor]*/ "/32");
			return val_buf;
		case P_ARP_OCTAVES:
			v += (RAW_SIZE * 10) / displaymax; // 1 based
			break;
		case P_MIDI_CH_IN:
		case P_MIDI_CH_OUT: {
			int midich = clampi(vscale, 0, 15) + 1;
			int n = sprintf(val_buf, "%d", midich);
			if (!dec_buf)
				dec_buf = val_buf + n;
			return val_buf;
		}
		case P_ARP_ORDER:
			return arm_mode_name[clampi(vscale, 0, NUM_ARP_ORDERS - 1)];
		case P_SEQ_ORDER:
			return seq_mode_name[clampi(vscale, 0, NUM_SEQ_ORDERS - 1)];
		case P_CV_QUANT:
			return cv_quant_name[clampi(vscale, 0, NUM_CV_QUANT_TYPES - 1)];
		case P_SCALE:
			return scale_name[clampi(vscale, 0, NUM_SCALES - 1)];
		case P_A_SHAPE:
		case P_B_SHAPE:
		case P_X_SHAPE:
		case P_Y_SHAPE:
			return lfo_shape_name[clampi(vscale, 0, NUM_LFO_SHAPES - 1)];
		case P_PITCH:
		case P_INTERVAL:
			displaymax = 120;
			break;
		case P_TEMPO:
			v += RAW_SIZE;
			// when listening to external clocks, we manually calculate the bpm_10x value as the pulses come in
			if (clock_type != CLK_INTERNAL)
				v = (bpm_10x * RAW_SIZE) / 1200;
			displaymax = 1200;
			break;
		case P_DLY_TIME:
			if (v < 0) {
				if (v <= -1024)
					v++;
				v = (-v * 13) / RAW_SIZE;
				int n = sprintf(val_buf, "%d", sync_divs_32nds[v]);
				if (!dec_buf)
					dec_buf = val_buf + n;
				sprintf(dec_buf, "/32 sync");
			}
			else {
				int n = sprintf(val_buf, "%d", (v * 100) / RAW_SIZE);
				if (!dec_buf)
					dec_buf = val_buf + n;
				sprintf(dec_buf, "free");
			}
			return val_buf;
		default:;
		}
	v = (v * displaymax) / RAW_SIZE;
	int av = abs(v);
	int n = sprintf(val_buf, "%c%d", (v < 0) ? '-' : ' ', av / 10);
	if (decimal) {
		if (!dec_buf)
			dec_buf = val_buf + n;
		sprintf(dec_buf, ".%d", av % 10);
	}
	return val_buf;
}

void take_param_snapshots(void) {
	param_snap = selected_param;
	src_snap = selected_mod_src;
}

// returns whether this drew anything
bool draw_cur_param(void) {
	gfx_text_color = 1;
	Param draw_param;
	// should we be drawing the param?
	switch (ui_mode) {
	case UI_DEFAULT:
		if (param_snap < NUM_PARAMS)
			// standard param editing
			draw_param = param_snap;
		else if (mem_param < NUM_PARAMS && enc_recently_used())
			// edited param with encoder => draw remembered param
			draw_param = mem_param;
		else
			// not editing
			return false;
		break;
	case UI_EDITING_A:
	case UI_EDITING_B:
		if (param_snap < NUM_PARAMS)
			draw_param = param_snap;
		else {
			// not editing => ask for param
			draw_str(0, 0, F_20_BOLD, mod_src_name[src_snap]);
			draw_str(0, 16, F_16, "select parameter");
			return true;
		}
		break;
	default:
		// other ui mode => don't draw anything
		return false;
		break;
	}

	// draw with upper shadow
	gfx_text_color = 2;

	const char* page_name = param_row_name[draw_param / 6];
	// manual page name overrides
	switch (draw_param) {
	case P_TEMPO:
	case P_SWING:
		page_name = I_TEMPO "Clock";
		break;
	case P_NOISE:
		page_name = I_WAVE "Noise";
		break;
	case P_CV_QUANT:
	case P_VOLUME:
		page_name = "System";
		break;
	default:
		break;
	}

	// draw page name, or mod source if one is selected
	draw_str(0, 0, F_12, src_snap == SRC_BASE ? page_name : mod_src_name[src_snap]);
	// draw param name
	const char* p_name = param_name[draw_param];
	if (str_width(F_16_BOLD, p_name) > 64)
		draw_str(0, 20, F_12_BOLD, p_name);
	else
		draw_str(0, 16, F_16_BOLD, p_name);

	char val_buf[32];
	u8 width = 0;
	s16 val = param_val_raw(draw_param, src_snap);
	// s32 vbase = val;
	// if (src_snap == SRC_BASE && draw_param != P_VOLUME) {
	// 	val = (param_val_unscaled(draw_param) * PARAM_SIZE) >> 16;
	// 	if (val != vbase) {
	// 		// if there is modulation going on, show the base value below
	// 		const char* val_str = get_param_str(draw_param, src_snap, vbase, val_buf, NULL);
	// 		width = str_width(F_8, val_str);
	// 		draw_str(OLED_WIDTH - 16 - width, 32 - 8, F_8, val_str);
	// 	}
	// }

	char dec_buf[16];
	const char* val_str = get_param_str(draw_param, src_snap, val, val_buf, dec_buf);
	u8 x = OLED_WIDTH - 15;
	if (*dec_buf)
		x -= str_width(F_8, dec_buf);
	Font font = F_28_BOLD; // F_24_BOLD is the first font that will be checked
	do {
		font--;
		width = str_width(font, val_str);
	} while (width >= 64 && font > F_12_BOLD);
	draw_str(x - width, 0, font, val_str);
	if (*dec_buf)
		draw_str(x, 0, F_8, dec_buf);
	return true;
}

void draw_arp_flag(void) {
	gfx_text_color = 0;
	if (arp_active()) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
	}
}

void draw_latch_flag(void) {
	gfx_text_color = 0;
	if (param_index(P_LATCH_TGL)) {
		fill_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
		draw_str(-(128 - 17), 32 - 7, F_8, "latch");
		if (seq_state() == SEQ_STEP_RECORDING)
			inverted_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
	}
}

bool is_snap_param(u8 x, u8 y) {
	u8 pA = x - 1 + y * 12;
	return param_snap < NUM_PARAMS && x > 0 && x < 7 && (param_snap == pA || param_snap == pA + 6);
}

s16 value_editor_column_led(u8 y) {
	if (param_snap >= NUM_PARAMS)
		return -1;

	u8 kontrast = 16;
	s16 k = 0;
	s16 v = param_val_raw(param_snap, src_snap);

	if (param_signed_or_mod(param_snap, src_snap)) {
		if (y < 4) {
			k = ((v - (3 - y) * RAW_QUART) * (192 * 4)) / RAW_SIZE;
			k = y * 2 * kontrast + clampi(k, 0, 191);
			if (y == 3 && v < 0)
				k = 255;
		}
		else {
			k = ((-v - (y - 4) * RAW_QUART) * (192 * 4)) / RAW_SIZE;
			k = (8 - y) * 2 * kontrast + clampi(k, 0, 191);
			if (y == 4 && v > 0)
				k = 255;
		}
	}
	else {
		k = ((v - (7 - y) * RAW_EIGHTH) * (192 * 8)) / RAW_SIZE;
		k = y * kontrast + clampi(k, 0, 191);
	}
	return clampi(k, 0, 255);
}

u8 ui_editing_led(u8 x, u8 y, u8 pulse) {
	u8 k = 0;
	if (x == 0) {
		if (param_snap < NUM_PARAMS)
			k = value_editor_column_led(y);
	}
	else if (x < 7) {
		u8 pAorB = x - 1 + y * 12 + (ui_mode == UI_EDITING_B ? 6 : 0);
		// holding down a mod source => light up params that are modulated by it
		if (mod_action_pressed() && src_snap != SRC_BASE && param_val_raw(pAorB, src_snap))
			k = 255;
		// pulse selected param
		if (pAorB == param_snap)
			k = pulse;
	}
	else {
		// pulse active mod source
		if (y == selected_mod_src)
			k = pulse;
		// light up mod sources that modulate current param
		else
			k = (y && param_val_raw(param_snap, y)) ? 255 : 0;
	}
	return k;
}

void param_shift_leds(u8 pulse) {
	leds[8][SS_SHIFT_A] = (ui_mode == UI_EDITING_A)                                                     ? 255
	                      : (ui_mode == UI_DEFAULT && param_snap < NUM_PARAMS && (param_snap % 12) < 6) ? pulse
	                                                                                                    : 0;
	leds[8][SS_SHIFT_B] = (ui_mode == UI_EDITING_B)                                                      ? 255
	                      : (ui_mode == UI_DEFAULT && param_snap < NUM_PARAMS && (param_snap % 12) >= 6) ? pulse
	                                                                                                     : 0;
}