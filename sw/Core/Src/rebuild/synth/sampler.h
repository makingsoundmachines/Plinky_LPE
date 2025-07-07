#pragma once
#include "synth.h"
#include "utils.h"

void apply_sample_lpg_noise(u8 voice_id, Voice* voice, Touch* s_touch, float goal_lpg, float noise_diff, float drive,
                            u32* dst);
