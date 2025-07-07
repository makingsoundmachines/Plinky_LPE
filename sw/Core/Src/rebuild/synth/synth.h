#pragma once
#include "utils.h"

#define NUM_VOICES 8
#define NUM_OSCILLATORS 4

extern bool cv_trig_high;

void handle_synth_voices(u32* dst);