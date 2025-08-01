#pragma once
#include "mem_defs.h"
#include "utils.h"

// this module manages 512KB (256 * 2048 bytes) of onboard flash
//
// this flash stores:
// - 1x SysParams
// - 32 presets
// - 96 sequencer pattern quarters
// - 8 SampleInfo's
// - calibration data

#define FLASH_ADDR_256 (0x08000000 + 256 * FLASH_PAGE_SIZE)

#define PATTERNS_START NUM_PRESETS
#define SAMPLES_START (PATTERNS_START + NUM_PATTERNS)

#define NUM_PTN_QUARTERS (NUM_PATTERNS * 4)
#define F_SAMPLES_START (PATTERNS_START + NUM_PTN_QUARTERS)
#define NUM_FLASH_ITEMS (F_SAMPLES_START + NUM_SAMPLES)

typedef enum FlashCalibType {
	FLASH_CALIB_NONE = 0b00,
	FLASH_CALIB_TOUCH = 0b01,
	FLASH_CALIB_ADC_DAC = 0b10,
	FLASH_CALIB_COMPLETE = 0b11,
} FlashCalibType;

extern bool flash_busy;

void check_bootloader_flash(void);
void init_flash();
void flash_toggle_preset(u8 preset_id);

// flash pointers

Preset* preset_flash_ptr(u8 preset_id);
PatternQuarter* ptn_quarter_flash_ptr(u8 quarter_id);
SampleInfo* sample_info_flash_ptr(u8 sample0);

// writing flash

void flash_erase_page(u8 page);
void flash_write_block(void* dst, void* src, int size);
void flash_write_page(void* src, u32 size, u8 page_id);

// calib

FlashCalibType flash_read_calib(void);
void flash_write_calib(FlashCalibType flash_calib_type);