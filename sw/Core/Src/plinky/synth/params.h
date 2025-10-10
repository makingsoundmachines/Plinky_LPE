#pragma once
#include "utils.h"

// this module deals with selecting parameters, editing their values, and applying mod-source modulations to them

// helpers
const Preset* init_params_ptr();
bool param_signed(Param param_id);
bool strip_available_for_synth(u8 strip_id);
void params_update_touch_pointers(void);
bool arp_active(void);

// main
void init_presets(void);
void params_tick(void);

// param retrieval calls
s32 param_val(Param param_id);
s32 param_val_poly(Param param_id, u8 string_id);
s8 param_index(Param param_id);
s8 param_index_poly(Param param_id, u8 string_id);

// save param calls
void save_param_raw(Param param_id, ModSource mod_src, s16 data);
void save_param_index(Param param_id, s8 index);

// pad action calls
void try_left_strip_for_params(u16 position, bool is_press_start);
bool press_param(u8 pad_id, u8 strip_id, bool is_press_start);
void select_mod_src(ModSource mod_src);
void reset_left_strip(void);

// shift state calls
void try_enter_edit_mode(bool mode_a);
void try_exit_edit_mode(bool param_select);

// encoder calls
void edit_param_from_encoder(s8 enc_diff, float enc_acc);
void params_toggle_default_value(void);
void hold_encoder_for_params(u16 duration);

// visuals
void take_param_snapshots(void);
bool params_want_to_draw(void);
void draw_cur_param(void);
void draw_arp_flag(void);
void draw_latch_flag(void);

bool is_snap_param(u8 x, u8 y);
s16 value_editor_column_led(u8 y);
u8 ui_editing_led(u8 x, u8 y, u8 pulse);
void param_shift_leds(u8 pulse);
