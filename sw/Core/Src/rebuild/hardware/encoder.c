#include "encoder.h"
#include "gfx/data/icons.h"
#include "gfx/gfx.h"
#include "synth/params.h"
#include "ui/pad_actions.h"
#include "ui/ui.h"

// needs cleanup
#include <math.h>
extern void ShowMessage(Font fnt, const char* msg, const char* submsg);
extern Preset const init_params;
extern u8 recsliceidx;
extern u32 ramtime[GEN_LAST];
extern int GetParam(u8 paramidx, u8 mod);
extern void EditParamNoQuant(u8 paramidx, u8 mod, s16 data);
extern void toggle_latch(void);
extern void toggle_arp(void);
extern SampleInfo* getrecsample(void);
// -- needs cleanup

volatile s8 encoder_value = 0;
volatile bool encoder_pressed = false;

static u8 prev_hardware_state; // should live in encoder_irq() if encoder_init() is removed
static float encoder_acc;
static u32 last_encoder_use = 0;

bool enc_recently_used(void) {
	return last_encoder_use > millis() - 2000;
}

void encoder_init(void) {
	// rj: I've taken this from main.c, not quite sure why it is necessary - one inaccurate tick of the encoder on
	// startup can't hurt anything can it?
	prev_hardware_state = (GPIOC->IDR >> 14) & 3;
	encoder_value = 2;
}

void encoder_irq(void) {
	static const s8 enc_deltas[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
	static s16 prev_encoder_value;

	// press
	encoder_pressed = !((GPIOC->IDR >> 13) & 1);

	// value
	u8 hardware_state = (GPIOC->IDR >> 14) & 3;
	u8 cur_state = prev_hardware_state | (hardware_state << 2);
	prev_hardware_state = hardware_state;
	prev_encoder_value = encoder_value;
	if (hardware_state == 0b11)
		encoder_value = (encoder_value & ~0b11) | 2; // snap to the middle of a detent (value = 4x + 2)
	else
		encoder_value += enc_deltas[cur_state];

	// acceleration
	encoder_acc *= 0.998f;
	encoder_acc += abs(prev_encoder_value - encoder_value) * 0.125f;
}

// this function is not cleaned up as 95% of it belongs in other modules that don't exist yet
void encoder_tick(void) {
	static u16 encoder_press_duration = 0;
	static bool prev_encoder_pressed;

	int enc_diff = encoder_value >> 2;

	// hold time
	if (encoder_pressed)
		encoder_press_duration++;

	// reboot belongs in system
	if (!encoder_pressed) {
		if (encoder_press_duration > 500) {
			HAL_Delay(500);
			HAL_NVIC_SystemReset();
		}
	}
	// reboot prep stage 2
	if (encoder_press_duration > 500) {
		ShowMessage(F_20_BOLD, "REBOOT!!", "");
	}
	// reboot prep stage 1
	else if (encoder_press_duration > 250) {
		ShowMessage(F_20_BOLD, "REBOOT?", "");
	}

	if ((enc_diff || encoder_pressed || prev_encoder_pressed)) {
		// log time
		last_encoder_use = millis();
		encoder_value -= enc_diff << 2;

		// basically all of this belongs in the parameter module
		int param_id = selected_param;
		if (param_id >= P_LAST)
			param_id = last_selected_param;
		switch (ui_mode) {
		case UI_DEFAULT:
		case UI_EDITING_A:
		case UI_EDITING_B:
			if (param_id < P_LAST) {
				// retrieve parameters
				int saved_param_value = GetParam(param_id, selected_mod_src);
				int prev_enc_val = saved_param_value;
				int flag_ = param_flags[param_id];
				bool is_signed = flag_ & FLAG_SIGNED;
				is_signed |= (selected_mod_src != M_BASE);
				// modify parameter value
				if ((flag_ & FLAG_MASK) && !selected_mod_src) {
					int maxi = flag_ & FLAG_MASK;
					saved_param_value += enc_diff * (FULL / maxi);
				}
				else {
					int enc_val_sens = 1;
					if (param_id == P_HEADPHONE)
						enc_val_sens = 4;
					saved_param_value +=
					    (int)floorf(0.5f + enc_diff * enc_val_sens * maxf(1.f, encoder_acc * encoder_acc));
				}
				saved_param_value = clampi(saved_param_value, is_signed ? -FULL : 0, FULL);

				// encoder button
				if (encoder_press_duration > 10) {
					if (encoder_press_duration >= 50) {
						ShowMessage(F_20_BOLD, I_CROSS "Mod Cleared", "");
						if (encoder_press_duration == 50) {
							for (int mod = 1; mod < M_LAST; ++mod)
								EditParamNoQuant(param_id, mod, (s16)0);
						}
					}
					else {
						ShowMessage(F_20_BOLD, I_CROSS "Clear Mod?", "");
					}
				}
				// button release, short press
				if (!encoder_pressed && prev_encoder_pressed && encoder_press_duration <= 50) {
					int base_val = (selected_mod_src) ? 0 : init_params.params[param_id][0];
					// toggle between 0 and set value
					if (base_val != 0) {
						if (saved_param_value != base_val)
							saved_param_value = base_val;
						else
							saved_param_value = 0;
					}
					else {
						if (saved_param_value != 0)
							saved_param_value = 0;
						else
							saved_param_value = FULL;
					}
				}
				// button press, toggle fake params
				if (encoder_pressed && !prev_encoder_pressed) {
					if (param_id == P_ARPONOFF) {
						toggle_arp();
					}
					else if (param_id == P_LATCHONOFF) {
						toggle_latch();
					}
				}
				// saved_param_value has changed since the start of the function
				if (prev_enc_val != saved_param_value) {
					// save it to memory
					EditParamNoQuant(param_id, selected_mod_src, (s16)saved_param_value);
				}
			}
			break;
		case UI_SAMPLE_EDIT:
			if (recsliceidx >= 0 && recsliceidx < 8) {
				SampleInfo* s = getrecsample();
				// set pitches for sample slices
				if (s->pitched) {
					int newnote = clampi(s->notes[recsliceidx] + enc_diff, 0, 96);
					if (newnote != s->notes[recsliceidx]) {
						s->notes[recsliceidx] = newnote;
						ramtime[GEN_SAMPLE] = millis();
					}
				}
				// set slice points for samples
				else {
					float smin = recsliceidx ? s->splitpoints[recsliceidx - 1] + 1024.f : 0.f;
					float smax = (recsliceidx < 7) ? s->splitpoints[recsliceidx + 1] - 1024.f : s->samplelen;

					float sp = clampf(s->splitpoints[recsliceidx] + enc_diff * 512, smin, smax);
					if (sp != s->splitpoints[recsliceidx]) {
						s->splitpoints[recsliceidx] = sp;
						ramtime[GEN_SAMPLE] = millis();
					}
				}
			}
			break;
		default:
			break;
		}
	}

	if (!encoder_pressed)
		encoder_press_duration = 0;
	prev_encoder_pressed = encoder_pressed;
}