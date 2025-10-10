#include "oled_viz.h"
#include "gfx/gfx.h"
#include "hardware/ram.h"
#include "pad_actions.h"
#include "settings_menu.h"
#include "shift_states.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"

// == TOOLS == //

#define RND(y) dither[(i & 3) + ((i / 128 + y) & 3) * 4]

static void gfx_dither_logo(u8 frame) {
	const static u8 dither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	const u8* l = get_logo() - 1;
	u8* v = oled_buffer() - 1;
	u8 k = frame / 2;
	for (u16 i = 0; i < 32 * 128 / 8; ++i) {
		u8 mask = 0;
		if (RND(0) < k)
			mask |= 1;
		if (RND(1) < k)
			mask |= 2;
		if (RND(2) < k)
			mask |= 4;
		if (RND(3) < k)
			mask |= 8;
		if (RND(4) < k)
			mask |= 16;
		if (RND(5) < k)
			mask |= 32;
		if (RND(6) < k)
			mask |= 64;
		if (RND(7) < k)
			mask |= 128;
		*v = (*v & mask) | (*l & ~mask);
		v++;
		l++;
	}
}

#undef RND

// == MESSAGE == //

static const char* message = 0;
static const char* submessage = 0;
static u8 message_font;
static u32 message_time = 0;

void flash_message(Font fnt, const char* msg, const char* submsg) {
	message = msg;
	submessage = submsg;
	message_font = fnt;
	message_time = millis() + 500;
}

// returns whether this drew anything
static bool draw_message(void) {
	if (!message)
		return false;
	if (millis() > message_time) {
		message = 0;
		return false;
	}
	if (submessage) {
		draw_str(0, 0, F_12, submessage);
		draw_str(0, 12, message_font, message);
		return true;
	}
	// vertically centered
	draw_str(0, 8, message_font, message);
	return true;
}

// == SCOPE == //

static u32 scope[OLED_WIDTH];

void clear_scope_pixel(u8 x) {
	scope[x] = 0;
}

void put_scope_pixel(u8 x, u8 y) {
	if (y >= 32)
		return;
	scope[x] |= (1 << y);
}

static void draw_scope(void) {
	u8* oled_buf = oled_buffer();
	for (u8 x = 0; x < OLED_WIDTH; ++x) {
		u32 m = scope[x];
		oled_buf[0] = m;
		oled_buf[128] = m >> 8;
		oled_buf[256] = m >> 16;
		oled_buf[384] = m >> 24;
		oled_buf++;
	}
}

// == MAIN == //

static char __attribute__((section(".endsection"))) version_tail[] = FIRMWARE_VERSION;

static void draw_startup_visuals(void) {
	static u8 frame = 3;
	if (frame == 255)
		return;
	if (frame < 36)
		gfx_dither_logo(frame);
	// draw version number
	gfx_text_color = 3;
	u8 y = maxi(frame - 255 + 32, 20);
	fdraw_str(47, y, F_12, param_index(P_LATCH_TGL) ? "v" FIRMWARE_VERSION : "LPE v" FIRMWARE_VERSION, version_tail);
	frame += 4;
}

static void draw_preset_info(void) {
	// top-left, priority: cued preset, last pressed note, current preset
	u8 xtab = draw_cued_preset_id();
	if (!xtab)
		xtab = draw_high_note();
	if (!xtab)
		xtab = draw_preset_id();
	// step recording fills the center of the screen
	if (seq_state() != SEQ_STEP_RECORDING)
		draw_preset_name(xtab);
	// bottom left priority: cued pattern, current pattern
	xtab = draw_cued_pattern_id(param_index(P_ARP_TGL));
	if (!xtab)
		draw_pattern_id(param_index(P_ARP_TGL));
}

static void draw_visuals(void) {
	gfx_text_color = 1;

	// There is a number of situations where a visual of just one or two fdraw_str() commands temporarily overrides the
	// regular visuals. We handle these first, and exit the function early so the regular visuals don't get rendered.
	// The order in which these are programmed defines their priority

	if (draw_message())
		return;
	if (shift_states_oled_visuals())
		return;
	if (pad_actions_oled_visuals())
		return;

	// build up the regular visuals, per ui mode

	switch (ui_mode) {
	case UI_DEFAULT:
		if (using_sampler())
			draw_sample_playback(&cur_sample_info);
		else
			draw_scope();
		draw_lfos();
		draw_max_pres();
		if (params_want_to_draw()) {
			draw_voices(false);
			draw_cur_param();
			return; // this fills the rest of the display
		}
		draw_preset_info();
		draw_voices(param_index(P_LATCH_TGL));
		draw_latch_flag();
		if (seq_state() == SEQ_STEP_RECORDING) {
			seq_draw_step_recording();
			return;
		}
		draw_arp_flag();
		draw_startup_visuals();
		break;
	case UI_EDITING_A:
	case UI_EDITING_B:
		draw_cur_param();
		break;
	case UI_PTN_START:
		seq_ptn_start_visuals();
		break;
	case UI_PTN_END:
		seq_ptn_end_visuals();
		break;
	case UI_LOAD:
		u8 xtab = draw_preset_id();
		draw_preset_name(xtab);
		draw_pattern_id(false);
		draw_sample_id();
		break;
	case UI_SAMPLE_EDIT:
		sampler_oled_visuals();
		break;
	case UI_SETTINGS_MENU:
		draw_settings_menu();
		break;
	}
}

void draw_oled_visuals(void) {
	oled_clear();
	draw_visuals();
	oled_flip();
}