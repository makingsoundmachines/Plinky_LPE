#include "ui.h"
#include "hardware/encoder.h"
#include "hardware/touchstrips.h"
#include "ui/pad_actions.h"

// ui mode
UIMode ui_mode = UI_DEFAULT;

void ui_frame(void) {
	// read touchstrips
	u8 read_phase = read_touchstrips();
	// actions handled once per touchstrip read cycle
	if (!read_phase) {
		handle_pad_action_long_presses();
		encoder_tick();
	}
}
