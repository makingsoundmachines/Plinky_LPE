#pragma once
#include "hardware/adc_dac.h"
#include "params.h"
#include "sampler.h"
#include "utils.h"

extern ValueSmoother ext_gain_smoother; // gain applied to external audio

// set the gain for external audio from the value of ADC_A_KNOB
static inline void init_ext_gain_for_recording(void) {
	set_smoother(&ext_gain_smoother, 65535 - adc_get_raw(ADC_A_KNOB));
}

// smoothly set the gain for the external audio, either from the P_INPUT_LVL parameter or from the value of ADC_A_KNOB
static inline void handle_ext_gain(void) {
	static u16 ext_gain_goal = 1 << 15;
	if (sampler_mode > SM_PREVIEW) {
		u16 gain_knob_value = 65535 - adc_get_raw(ADC_A_KNOB);
		if (abs(gain_knob_value - ext_gain_goal) > 256) // hysteresis
			ext_gain_goal = gain_knob_value;
	}
	else
		ext_gain_goal = param_val(P_INPUT_LVL);
	smooth_value(&ext_gain_smoother, ext_gain_goal, 65536.f);
}