#pragma once
#include "synth/params.h"
#include "utils.h"

// Any non-synth functionality on the main 8 x 8 grid of pads is called a pad action. This includes selecting parameters
// and mod sources, editing sample slice points, selecting/loading/copying in the load preset ui, etc.

void handle_pad_actions(u8 strip_id, Touch* strip_cur);
void handle_pad_action_long_presses(void);

// for ui.h
extern u8 strip_holds_valid_action;
extern u8 strip_is_action_pressed;
extern u16 long_press_frames;
extern u8 long_press_pad;