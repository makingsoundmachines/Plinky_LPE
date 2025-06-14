#include "ui.h"
#include "hardware/touchstrips.h"

// ui mode
UIMode ui_mode = UI_DEFAULT;

// editing params
u8 edit_param = 255;
u8 last_edit_param = 255;
u8 edit_mod = 0;

void ui_frame(void) {
	// read touchstrips
	read_touchstrips();
}
