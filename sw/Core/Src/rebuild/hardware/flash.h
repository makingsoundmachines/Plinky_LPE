#pragma once
#include "utils.h"

// this module manages 512KB (256 * 2048 bytes) of onboard flash
//
// this flash stores:
// - 1x SysParams
// - 32 presets
// - 96 sequencer pattern quarters
// - 8 SampleInfo's
// - calibration data (in page 255)

#define FLASH_ADDR_256 (0x08000000 + 256 * FLASH_PAGE_SIZE)

#define PATTERNS_START NUM_PRESETS
#define SAMPLES_START (PATTERNS_START + NUM_PATTERNS)

#define NUM_PTN_QUARTERS (NUM_PATTERNS * 4)
#define F_SAMPLES_START (PATTERNS_START + NUM_PTN_QUARTERS)
#define NUM_FLASH_ITEMS (F_SAMPLES_START + NUM_SAMPLES)

typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;

typedef struct FlashPage {
	union {
		u8 raw[FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter)];
		Preset preset;
		PatternQuarter pattern_quarter;
		SampleInfo sample_info;
	};
	SysParams sys_params;
	PageFooter footer;
} FlashPage;
// static_assert(sizeof(FlashPage) == 2048, "?");

extern bool flash_busy;

void init_flash();
void flash_toggle_preset(u8 preset_id);

// flash pointers

Preset* preset_flash_ptr(u8 preset_id);
PatternQuarter* ptr_quarter_flash_ptr(u8 quarter_id);
SampleInfo* sample_info_flash_ptr(u8 sample0);

// writing flash

void flash_erase_page(u8 page);
void flash_write_block(void* dst, void* src, int size);
void flash_write_page(void* src, u32 size, u8 page_id);
