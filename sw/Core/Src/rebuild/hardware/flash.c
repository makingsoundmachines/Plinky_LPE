#include "flash.h"

// cleanup
extern Preset const init_params;
extern SysParams sys_params;
// -- cleanup

static u8 latest_page_id[NUM_FLASH_ITEMS] = {};
static u8 backup_page_id[NUM_PRESETS] = {};
static u8 next_free_page = 0;
static u32 next_seq = 0;

bool flash_busy = false;

// flash pointers

static u16 compute_hash(const void* data, int nbytes) {
	u16 hash = 123;
	const u8* src = (const u8*)data;
	for (int i = 0; i < nbytes; ++i)
		hash = hash * 23 + *src++;
	return hash;
}

static FlashPage* flash_page_ptr(u8 page) {
	return (FlashPage*)(FLASH_ADDR_256 + page * FLASH_PAGE_SIZE);
}

Preset* preset_flash_ptr(u8 preset_id) {
	if (preset_id >= NUM_PRESETS)
		return (Preset*)&init_params;
	FlashPage* fp = flash_page_ptr(latest_page_id[preset_id]);
	if (fp->footer.idx != preset_id || fp->footer.version != CUR_PRESET_VERSION)
		return (Preset*)&init_params;
	return (Preset*)fp;
}

PatternQuarter* ptr_quarter_flash_ptr(u8 quarter_id) {
	if (quarter_id >= NUM_PTN_QUARTERS)
		return (PatternQuarter*)zero;
	FlashPage* fp = flash_page_ptr(latest_page_id[PATTERNS_START + quarter_id]);
	if (fp->footer.idx != PATTERNS_START + quarter_id || fp->footer.version != CUR_PRESET_VERSION)
		return (PatternQuarter*)zero;
	return (PatternQuarter*)fp;
}

SampleInfo* sample_info_flash_ptr(u8 sample0) {
	if (sample0 >= NUM_SAMPLES)
		return (SampleInfo*)zero;
	FlashPage* fp = flash_page_ptr(latest_page_id[F_SAMPLES_START + sample0]);
	if (fp->footer.idx != F_SAMPLES_START + sample0 || fp->footer.version != CUR_PRESET_VERSION)
		return (SampleInfo*)zero;
	return (SampleInfo*)fp;
}

// main

void init_flash() {
	u8 dummy_page = 0;
	memset(latest_page_id, dummy_page, sizeof(latest_page_id));
	memset(backup_page_id, dummy_page, sizeof(backup_page_id));
	u32 highest_seq = 0;
	next_free_page = 0;
	memset(&sys_params, 0, sizeof(sys_params));
	// scan for the latest page for each object
	for (u8 page = 0; page < 255; ++page) {
		FlashPage* p = flash_page_ptr(page);
		u8 i = p->footer.idx;
		if (i >= NUM_FLASH_ITEMS)
			continue; // skip blank
		if (p->footer.version < CUR_PRESET_VERSION)
			continue; // skip old
		u16 check = compute_hash(p, 2040);
		if (check != p->footer.crc) {
			DebugLog("flash page %d has a bad crc!\r\n", page);
			if (page == dummy_page) {
				// shit, the dummy page is dead! move to a different dummy
				for (u8 i = 0; i < NUM_FLASH_ITEMS; ++i)
					if (latest_page_id[i] == dummy_page)
						latest_page_id[i]++;
				for (u8 i = 0; i < NUM_PRESETS; ++i)
					if (backup_page_id[i] == dummy_page)
						backup_page_id[i]++;
				dummy_page++;
			}
			continue;
		}
		if (p->footer.seq > highest_seq) {
			highest_seq = p->footer.seq;
			next_free_page = page + 1;
			sys_params = p->sys_params;
		}
		FlashPage* existing = flash_page_ptr(latest_page_id[i]);
		if (existing->footer.idx != i || p->footer.seq > existing->footer.seq || existing->footer.version < 2)
			latest_page_id[i] = page;
	}
	next_seq = highest_seq + 1;
	memcpy(backup_page_id, latest_page_id, sizeof(backup_page_id));
}

void flash_toggle_preset(u8 preset_id) {
	u8 temp = backup_page_id[preset_id];
	backup_page_id[preset_id] = latest_page_id[preset_id];
	latest_page_id[preset_id] = temp;
}

// writing flash

void flash_erase_page(u8 page) {
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	SET_BIT(FLASH->CR, FLASH_CR_BKER); // bank 2
	MODIFY_REG(FLASH->CR, FLASH_CR_PNB, ((page & 0xFFU) << FLASH_CR_PNB_Pos));
	SET_BIT(FLASH->CR, FLASH_CR_PER);
	SET_BIT(FLASH->CR, FLASH_CR_STRT);
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	CLEAR_BIT(FLASH->CR, (FLASH_CR_PER | FLASH_CR_PNB));
	// u32* mem = (u32*)(FLASH_ADDR_256 + page * 2048);
	// for (int i = 0; i < 2048 / 4; ++i)
	// 	if (mem[i] != 0xffffffff) {
	// 		DebugLog("flash mem page %d failed to erase at address %d - val %08x\r\n", page, i * 4, mem[i]);
	// 		break;
	// 	}
}

void flash_write_block(void* dst, void* src, int size) {
	u64* s = (u64*)src;
	volatile u64* d = (volatile u64*)dst;
	// int osize = size;
	while (size >= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
		size -= 8;
	}
	// int fail = 0;
	// {
	// 	u64* s = (u64*)src;
	// 	volatile u64* d = (volatile u64*)dst;
	// 	for (int i = 0; i < osize; i += 8) {
	// 		if (*s != *d) {
	// 			u32 s0 = (*s);
	// 			u32 s1 = (*s >> 32);
	// 			u32 d0 = (*d);
	// 			u32 d1 = (*d >> 32);
	// 			DebugLog("flash program failed at offset %d - %08x %08x vs dst %08x %08x\r\n", i, s0, s1, d0, d1);
	// 			++fail;
	// 		}
	// 		s++;
	// 		d++;
	// 	}
	// }
	// if (fail != 0)
	// 	DebugLog("flash program block failed!\r\n");
}

void flash_write_page(void* src, u32 size, u8 page_id) {
	flash_busy = true;
	HAL_FLASH_Unlock();
	bool in_use;
	do {
		FlashPage* p = flash_page_ptr(next_free_page);
		in_use = next_free_page == 255;
		in_use |= (p->footer.idx < NUM_FLASH_ITEMS && latest_page_id[p->footer.idx] == next_free_page);
		in_use |= (p->footer.idx < NUM_PRESETS && backup_page_id[p->footer.idx] == next_free_page);
		if (in_use)
			++next_free_page;
	} while (in_use);
	flash_erase_page(next_free_page);
	u8 flash_page = next_free_page++;
	u8* dst = (u8*)(FLASH_ADDR_256 + flash_page * FLASH_PAGE_SIZE);
	flash_write_block(dst, src, size);
	flash_write_block(dst + FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter), &sys_params, sizeof(SysParams));
	PageFooter footer;
	footer.idx = page_id;
	footer.seq = next_seq++;
	footer.version = CUR_PRESET_VERSION;
	footer.crc = compute_hash(dst, 2040);
	flash_write_block(dst + 2040, &footer, 8);
	HAL_FLASH_Lock();
	latest_page_id[page_id] = flash_page;
	flash_busy = false;
}