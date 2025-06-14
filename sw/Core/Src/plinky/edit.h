#include "synth/params.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

#include "ui/pad_actions.h"

void toggle_arp(void) {
	rampreset.flags ^= FLAGS_ARP;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_ARP)) ? "arp on" : "arp off", 0);
	ramtime[GEN_SYS] = millis();
}
void clear_latch(void);
void toggle_latch(void) {
	rampreset.flags ^= FLAGS_LATCH;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_LATCH)) ? "latch on" : "latch off", 0);
	ramtime[GEN_SYS] = millis();
	if (!((rampreset.flags & FLAGS_LATCH)))
		clear_latch();
}

extern float encaccel;
int lastencodertime = 0;
static u8 prevencbtn;
static int encbtndowntime = 0;

void encoder_editing() {
	int ev = encval >> 2;
	if (encbtn)
		encbtndowntime++;
	if (!encbtn) {
		if (encbtndowntime > 500) {
			HAL_Delay(500);
		}
	}
	if (encbtndowntime > 500) {
		ShowMessage(F_20_BOLD, "REBOOT!!", "");
	}
	else if (encbtndowntime > 250) {
		ShowMessage(F_20_BOLD, "REBOOT?", "");
	}
	if ((ev || encbtn || prevencbtn)) {
		// ENCODER EDITING
		lastencodertime = millis();
		encval -= ev << 2;
		int pi = selected_param;
		if (pi >= P_LAST)
			pi = last_selected_param;
		if ((pi < P_LAST) && (ui_mode == UI_DEFAULT || ui_mode == UI_EDITING_A || ui_mode == UI_EDITING_B)) {
			int cur = GetParam(pi, selected_mod_src);
			int prev = cur;
			int f = param_flags[pi];
			bool issigned = f & FLAG_SIGNED;
			issigned |= (selected_mod_src != M_BASE);
			if ((f & FLAG_MASK) && selected_mod_src == 0) {
				int maxi = f & FLAG_MASK;
				cur += ev * (FULL / maxi);
			}
			else {
				int ev_sens = 1;
				if (pi == P_HEADPHONE)
					ev_sens = 4;
				cur += (int)floorf(0.5f + ev * ev_sens * maxf(1.f, encaccel * encaccel));
			}
			cur = clampi(cur, issigned ? -FULL : 0, FULL);
			if (encbtndowntime > 10) {
				if (encbtndowntime >= 50) {
					ShowMessage(F_20_BOLD, I_CROSS "Mod Cleared", "");
					if (encbtndowntime == 50) {
						for (int mod = 1; mod < M_LAST; ++mod)
							EditParamNoQuant(pi, mod, (s16)0);
					}
				}
				else {
					ShowMessage(F_20_BOLD, I_CROSS "Clear Mod?", "");
				}
			}
			if (!encbtn && prevencbtn && encbtndowntime <= 50) {
				int deflt = (selected_mod_src) ? 0 : init_params.params[pi][0];
				if (deflt != 0) {
					if (cur != deflt)
						cur = deflt;
					else
						cur = 0;
				}
				else {
					if (cur != 0)
						cur = 0;
					else
						cur = FULL;
				}
			}
			if (encbtn && !prevencbtn) {
				if (pi == P_ARPONOFF) {
					toggle_arp();
				}
				else if (pi == P_LATCHONOFF) {
					toggle_latch();
				}
				else {
					// used to do clear-param here, but now we do it on RELEASE!
				}
			}
			if (prev != cur) {
				EditParamNoQuant(pi, selected_mod_src, (s16)cur);
			}
		}
		else if (ui_mode == UI_SAMPLE_EDIT && recsliceidx >= 0 && recsliceidx < 8) {
			SampleInfo* s = getrecsample();
			if (s->pitched) {
				int newnote = clampi(s->notes[recsliceidx] + ev, 0, 96);
				if (newnote != s->notes[recsliceidx]) {
					s->notes[recsliceidx] = newnote;
					ramtime[GEN_SAMPLE] = millis();
				}
			}
			else {
				float smin = recsliceidx ? s->splitpoints[recsliceidx - 1] + 1024.f : 0.f;
				float smax = (recsliceidx < 7) ? s->splitpoints[recsliceidx + 1] - 1024.f : s->samplelen;

				float sp = clampf(s->splitpoints[recsliceidx] + ev * 512, smin, smax);
				if (sp != s->splitpoints[recsliceidx]) {
					s->splitpoints[recsliceidx] = sp;
					ramtime[GEN_SAMPLE] = millis();
				}
			}
		} // sample mode
	} // en/encbtn
	if (!encbtn)
		encbtndowntime = 0;
	prevencbtn = encbtn;
}