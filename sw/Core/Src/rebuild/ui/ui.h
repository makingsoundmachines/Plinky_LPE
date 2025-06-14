#pragma once
#include "utils.h"

typedef enum UIMode {
	UI_DEFAULT,     // regular playing mode
	UI_EDITING_A,   // editing any of the A parameters
	UI_EDITING_B,   // editing any of the B parameters
	UI_PTN_START,   // setting the start of the sequencer pattern
	UI_PTN_END,     // setting the end of the sequencer pattern
	UI_LOAD,        // load screen: preset / pattern / sample
	UI_SAMPLE_EDIT, // sample edit screen
} UIMode;

extern UIMode ui_mode;

void ui_frame(void);