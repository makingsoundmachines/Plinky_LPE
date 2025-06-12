#pragma once
#include "mem_defs.h"
#include "utils.h"

// this module manages the preset, pattern quarters, SampleInfo and SysParams that are curently loaded into ram
// it manages retrieving data from, and writing data to onboard flash when needed

extern SysParams sys_params;

extern u8 cur_sample_id;           // possibly remove after SPI cleanup
extern Preset cur_preset;          // could be made local by optimizing sequencer & modulation
extern SampleInfo cur_sample_info; // possibly give sampler its own copy

// ui only
extern u8 cur_pattern_id;
extern u8 recent_load_item;
extern u8 cued_preset_id;
extern u8 cued_pattern_id;
extern u8 cued_sample_id;
extern u8 copy_preset_id;

// get ram state
bool pattern_outdated(void);

// fake params
bool arp_on(void);
void save_arp(bool on);
bool latch_on(void);
void save_latch(bool on);

// main
PatternStringStep* string_step_ptr(u8 string_id, bool only_filled);

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
void cue_ram_item(u8 item_id);
void try_apply_cued_ram_item(u8 item_id);
