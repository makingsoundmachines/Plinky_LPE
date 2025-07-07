#include "hardware/adc_dac.h"
#include "hardware/touchstrips.h"
#include "synth/sampler.h"
#include "synth/strings.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

typedef struct CalibProgress {
	float weight[8];
	float pos[8];
	float pres[8];
} CalibProgress;
static inline CalibProgress* GetCalibProgress(int sensoridx) {
	CalibProgress* p = (CalibProgress*)delaybuf;
	return p + sensoridx;
}
CalibResult calibresults[18];

euclid_state arp_rhythm;
euclid_state seq_rhythm;

void check_curstep(void) { // enforces invariants
	if (rampreset.looplen_step <= 0 || rampreset.looplen_step > 64)
		rampreset.looplen_step = 64;
	rampreset.loopstart_step_no_offset &= 63;
	u8 loopstart_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;
	cur_step = (cur_step - loopstart_step) % rampreset.looplen_step;
	if (cur_step < 0)
		cur_step += rampreset.looplen_step;
	cur_step += loopstart_step;
}

void set_cur_step(u8 newcurstep, bool triggerit) {
	cur_step = newcurstep;
	check_curstep();
	seq_rhythm.did_a_retrig = triggerit; // make the sound play out once
	ticks_since_step = 0;
	seq_divide_counter = 0;
}

void OnLoop(void) {
	if (pending_preset != 255) {
		SetPreset(pending_preset, false);
		pending_preset = 255;
	}
	if (pending_pattern != 255) {
		EditParamQuant(P_SEQPAT, M_BASE, pending_pattern);
		pending_pattern = 255;
	}
	if (pending_loopstart_step != 255) {
		u8 loopstart_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;
		if (loopstart_step != pending_loopstart_step) {
			rampreset.loopstart_step_no_offset = (pending_loopstart_step - step_offset) & 63;
			ramtime[GEN_PRESET] = millis();
		}
		set_cur_step(loopstart_step, seq_rhythm.did_a_retrig);
		pending_loopstart_step = 255;
	}
	if (pending_sample1 != cur_sample_id1 && pending_sample1 != 255) {
		EditParamQuant(P_SAMPLE, 0, pending_sample1);
		pending_sample1 = 255;
	}
	check_curstep();
}

bool got_ui_reset = false;
int tap_count = 0;
void clear_latch(void);

void reverb_clear(void) {
	memset(reverbbuf, 0, (RVMASK + 1) * 2);
}
void delay_clear(void) {
	memset(delaybuf, 0, (DLMASK + 1) * 2);
}

u16 audioin_holdtime = 0;
s16 audioin_peak = 0;
s16 audioin_hold = 0;
static inline u8 getwaveform4(SampleInfo* s, int x) { // x is 0-2047
	if (x < 0 || x >= 2048)
		return 0;
	return (s->waveform4_b[x >> 1] >> ((x & 1) * 4)) & 15;
}
static inline u8 getwaveform4halfres(SampleInfo* s, int x) { // x 0-1023
	u8 b = s->waveform4_b[x & 1023];
	return maxi(b & 15, b >> 4);
}
static inline u16 getwaveform4zoom(SampleInfo* s, int x, int zoom) { // x is 0-2048. returns average and peak!
	if (zoom <= 0)
		return getwaveform4(s, x >> zoom);
	int samplepairs = 1 << (zoom - 1);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	int avg = 0, peak = 0;
	u8* bend = &s->waveform4_b[1024];
	for (int i = 0; i < samplepairs && b < bend; ++i, ++b) {
		int s0 = b[0] & 15;
		int s1 = b[0] >> 4;
		avg += s0 + s1;
		peak = maxi(peak, maxi(s0, s1));
	}
	avg >>= zoom;
	return avg + peak * 256;
}

static inline void setwaveform4(SampleInfo* s, int x, int v) {
	v = clampi(v, 0, 15);
	u8* b = &s->waveform4_b[(x >> 1) & 1023];
	if (x & 1) {
		v = maxi(v, (*b) >> 4);
		*b = (*b & 0x0f) | (v << 4);
	}
	else {
		v = maxi(v, (*b) & 15);
		*b = (*b & 0xf0) | v;
	}
}

void DebugSPIPage(int addr);

void seq_step(int initial);

void arp_reset(void);
void ShowMessage(Font fnt, const char* msg, const char* submsg);

void toggle_arp(void);

int param_eval_finger(u8 paramidx, int fingeridx, Touch* f);
bool read_from_seq = false;

FingerRecord* readpattern(int fi) {
	if (rampattern_idx != cur_pattern || shift_state == SS_CLEAR)
		return 0;
	FingerRecord* fr = &rampattern[(cur_step >> 4) & 3].steps[cur_step & 15][fi];
	// does any substep hold data?
	int record_pressure = 0;
	for (u8 i = 0; i < 8; i++)
		record_pressure += fr->pres[i];
	return record_pressure == 0 ? 0 : fr;
}
