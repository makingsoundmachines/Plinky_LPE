#pragma once
#include "utils.h"

// this module manages the preset, pattern quarters, SampleInfo and SysParams that are curently loaded into ram
// it manages retrieving data from, and writing data to onboard flash when needed

extern SysParams sys_params;

extern u8 cur_sample_id;           // possibly remove after SPI cleanup
extern Preset cur_preset;          // could be made local by optimizing sequencer & modulation
extern SampleInfo cur_sample_info; // possibly give sampler its own copy

// get ram state
bool pattern_outdated(void);

// main
PatternStringStep* string_step_ptr(u8 string_id, bool only_filled, u8 seq_step);

void init_ram(void);
void ram_frame(void);

// update ram
void log_ram_edit(RamSegment segment);
bool update_preset_ram(bool force);
void update_pattern_ram(bool force);
void update_sample_ram(bool force);

// save / load
void load_preset(u8 preset_id, bool force);
void load_sample(u8 sample_id);

void touch_load_item(u8 item_id);
void clear_load_item(void);
void copy_load_item(u8 item_id);
bool apply_cued_load_items(void);
void cue_ram_item(u8 item_id, u8 long_press_pad);
void try_apply_cued_ram_item(u8 item_id);

// visuals
u8 draw_cued_preset_id(void);
u8 draw_high_note(void);
u8 draw_preset_id(void);
u8 draw_cued_pattern_id(bool with_arp_icon);
void draw_pattern_id(bool with_arp_icon);
void draw_preset_name(u8 xtab);
void draw_sample_id(void);
void draw_select_load_item(u8 item_id, bool done);
void draw_clear_load_item(bool done);

u8 ui_load_led(u8 x, u8 y, u8 pulse);