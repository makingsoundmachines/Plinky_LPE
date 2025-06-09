#if defined(_WIN32) || defined(__APPLE__)
// #define EMU
#pragma warning(disable : 4244)
#endif

#ifdef WASM
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifndef EMU
#include <main.h>

extern DMA_HandleTypeDef hdma_adc1;

extern DMA_HandleTypeDef hdma_dac_ch1;
extern DMA_HandleTypeDef hdma_dac_ch2;

extern I2C_HandleTypeDef hi2c2;

extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockB1;
extern DMA_HandleTypeDef hdma_sai1_a;
extern DMA_HandleTypeDef hdma_sai1_b;

extern SPI_HandleTypeDef hspi2;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern DMA_HandleTypeDef hdma_spi2_rx;

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

#endif

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define IMPL
#ifdef WASM
#define ASSERT(...)
#else
#define ASSERT assert
#endif
#include "core.h"
#include "defs/lfo.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/flash.h"
#include "hardware/leds.h"
#include "hardware/midi.h"
#include "hardware/ram.h"
#include "hardware/spi.h"
#include "low_level/codec.h"
#include "synth/arp.h"
#include "synth/audio.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "testing/tick_counter.h"
#include "ui/oled_viz.h"
#include "ui/ui.h"

#define TWENTY_OVER_LOG2_10 6.02059991328f // (20.f/log2(10.f));

static inline float lin2db(float lin) {
	return log2f(lin) * TWENTY_OVER_LOG2_10;
}
static inline float db2lin(float db) {
	return exp2f(db * (1.f / TWENTY_OVER_LOG2_10));
}

TickCounter _tc_budget;
TickCounter _tc_all;
TickCounter _tc_fx;
TickCounter _tc_audio;
TickCounter _tc_touch;
TickCounter _tc_led;
TickCounter _tc_filter;

// u8 ui_edit_param_prev[2][4] = {
// {NUM_PARAMS,NUM_PARAMS,NUM_PARAMS,NUM_PARAMS},{NUM_PARAMS,NUM_PARAMS,NUM_PARAMS,NUM_PARAMS} }; // push to front
// history

#ifdef EMU
short delaybuf[DLMASK + 1];
short reverbbuf[RVMASK + 1];
int emupitchsense;
int emugatesense;
#else
short* reverbbuf = (short*)0x10000000; // use ram2 :)
short* delaybuf = (short*)0x20008000;  // use end of ram1 :)

#endif
static int reverbpos = 0;

static int k_reverb_fade = 240;
static int k_reverb_shim = 240;
static float k_reverb_wob = 0.5f;
static int k_reverbsend = 0;
static int shimmerpos1 = 2000;
static int shimmerpos2 = 1000;
static int shimmerfade = 0;
static int dshimmerfade = 32768 / 4096;

static lfo aplfo = LFOINIT(1.f / 32777.f * 9.4f);
static lfo aplfo2 = LFOINIT(1.3f / 32777.f * 3.15971f);

/*

update
kick fetch for this pos
*/

// these includes are sensitive to how they are ordered
// turning off formatting so that they don't get reordered alphabetically

// clang-format off

#include "touch.h"
#include "calib.h"

#include "../webusb.h"

// clang-format on

inline s32 trifold(u32 x) {
	if (x > 0x80000000)
		x = 0xffffffff - x;
	return (s32)(x >> 4);
}

#ifdef EMU
float arpdebug[1024];
int arpdebugi;
#endif

s32 Reverb2(s32 input, s16* buf) {
	int i = reverbpos;
	int outl = 0, outr = 0;
	float wob = lfo_next(&aplfo) * k_reverb_wob;
	int apwobpos = FLOAT2FIXED((wob + 1.f), 12 + 6);
	wob = lfo_next(&aplfo2) * k_reverb_wob;
	int delaywobpos = FLOAT2FIXED((wob + 1.f), 12 + 6);
#define RVDIV / 2
#define CHECKACC // assert(acc>=-32768 && acc<32767);
#define AP(len)                                                                                                        \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RVMASK;                                                                              \
		s16 d = buf[j];                                                                                                \
		acc -= d >> 1;                                                                                                 \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = (acc >> 1) + d;                                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define AP_WOBBLE(len, wobpos)                                                                                         \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RVMASK;                                                                              \
		s16 d = LINEARINTERPRV(buf, j, wobpos);                                                                        \
		acc -= d >> 1;                                                                                                 \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = (acc >> 1) + d;                                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define DELAY(len)                                                                                                     \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RVMASK;                                                                              \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = buf[j];                                                                                                  \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}
#define DELAY_WOBBLE(len, wobpos)                                                                                      \
	{                                                                                                                  \
		int j = (i + len RVDIV) & RVMASK;                                                                              \
		buf[i] = SATURATE16(acc);                                                                                      \
		acc = LINEARINTERPRV(buf, j, wobpos);                                                                          \
		i = j;                                                                                                         \
		CHECKACC                                                                                                       \
	}

	// Griesinger according to datorro does 142, 379, 107, 277 on the way in - totoal 905 (20ms)
	// then the loop does 672+excursion, delay 4453, (damp), 1800, delay 3720 - total 10,645 (241ms)
	// then decay, and feed in
	// and on the other side 908+excursion,	delay 4217, (damp), 2656, delay 3163 - total 10,944 (248 ms)

	// keith barr says:
	// I really like 2AP, delay, 2AP, delay, in a loop.
	// I try to set the delay to somewhere a bit less than the sum of the 2 preceding AP delays,
	// which are of course much longer than the initial APs(before the loop)
	// Yeah, the big loop is great; you inject input everywhere, but take it out in only two places
	// It just keeps comin� newand fresh as the thing decays away.�If you�ve got the memoryand processing!

	// lets try the 4 greisinger initial Aps, inject stereo after the first AP,

	int acc = ((s16)(input)) * k_reverbsend >> 17;
	AP(142);
	AP(379);
	acc += (input >> 16) * k_reverbsend >> 17;
	AP(107);
	AP(277);
	int reinject = acc;
	static int fb1 = 0;
	acc += fb1;
	AP_WOBBLE(672, apwobpos);
	AP(1800);
	DELAY(4453);

	if (1) {
		// shimmer - we can read from up to about 2000 samples ago

		// Brief shimmer walkthrough:
		// - We walk backwards through the reverb buffer with 2 indices: shimmerpos1 and shimmerpos2.
		//   - shimmerpos1 is the *previous* shimmer position.
		//   - shimmerpos2 is the *current* shimmer position.
		//   - Note that we add these to i (based on reverbpos), which is also walking backwards
		//     through the buffer.
		// - shimmerfade controls the crossfade between the shimmer from shimmerpos1 and shimmerpos2.
		//   - When shimmerfade == 0, shimmerpos1 (the old shimmer) is chosen.
		//   - When shimmerfade == SHIMMER_FADE_LEN - 1, shimmerpos2 (the new shimmer) is chosen.
		//   - For everything in-between, we linearly interpolate (crossfade).
		//   - When we hit the end of the fade, we reset shimmerpos2 to a random new position and set
		//     shimmerpos1 to the old shimmerpos2.
		// - dshimmerfade controls the speed at which we fade.

#define SHIMMER_FADE_LEN 32768
		shimmerfade += dshimmerfade;

		if (shimmerfade >= SHIMMER_FADE_LEN) {
			shimmerfade -= SHIMMER_FADE_LEN;

			shimmerpos1 = shimmerpos2;
			shimmerpos2 = (rand() & 4095) + 8192;
			dshimmerfade =
			    (rand() & 7) + 8; // somewhere between SHIMMER_FADE_LEN/2048 and SHIMMER_FADE_LEN/4096 ie 8 and 16
		}

		// L = shimmer from shimmerpos1, R = shimmer from shimmerpos2
		u32 shim1 = STEREOPACK(buf[(i + shimmerpos1) & RVMASK], buf[(i + shimmerpos2) & RVMASK]);
		u32 shim2 = STEREOPACK(buf[(i + shimmerpos1 + 1) & RVMASK], buf[(i + shimmerpos2 + 1) & RVMASK]);
		u32 shim = STEREOADDAVERAGE(shim1, shim2);

		// Fixed point crossfade:
#ifdef CORTEX
		u32 a = STEREOPACK((SHIMMER_FADE_LEN - 1) - shimmerfade, shimmerfade);
		s32 shimo;
		asm("smuad %0, %1, %2" : "=r"(shimo) : "r"(a), "r"(shim));
#else
		STEREOUNPACK(shim);
		s32 shimo = shiml * ((SHIMMER_FADE_LEN - 1) - shimmerfade) + shimr * shimmerfade;
#endif
		shimo >>= 15; // Divide by SHIMMER_FADE_LEN

		// Apply user-selected shimmer amount.
		shimo *= k_reverb_shim;
		shimo >>= 8;

		// Tone down shimmer amount.
		shimo >>= 1;

		acc += shimo;
		outl = shimo;
		outr = shimo;

		shimmerpos1--;
		shimmerpos2--;
	}

	const static float k_reverb_color = 0.95f;
	static float lpf = 0.f, dc = 0.f;
	lpf += (((acc * k_reverb_fade) >> 8) - lpf) * k_reverb_color;
	dc += (lpf - dc) * 0.005f;
	acc = (int)(lpf - dc);
	outl += acc;

	acc += reinject;
	AP_WOBBLE(908, delaywobpos);
	AP(2656);
	DELAY(3163);
	static float lpf2 = 0.f;
	lpf2 += (((acc * k_reverb_fade) >> 8) - lpf2) * k_reverb_color;
	acc = (int)(lpf2);

	outr += acc;

	reverbpos = (reverbpos - 1) & RVMASK;
	fb1 = (acc * k_reverb_fade) >> 8;
	return STEREOPACK(SATURATE16(outl), SATURATE16(outr));
}

u32 STEREOSIGMOID(u32 in) {
	u16 l = sigmoid[(u16)in];
	u16 r = sigmoid[in >> 16];
	return STEREOPACK(l, r);
}

s16 MONOSIGMOID(int in) {
	in = SATURATE16(in);
	return sigmoid[(u16)in];
}

#ifdef EMU
float powerout; // squared power
float gainhistoryrms[512];
int ghi;
#endif

#ifdef EMU
float m_compressor, m_dry, m_audioin, m_dry2wet, m_delaysend, m_delayreturn, m_reverbin, m_reverbout, m_fxout, m_output;
void MONITORPEAK(float* mon, u32 stereoin) {
	STEREOUNPACK(stereoin);
	float peak = (1.f / 32768.f) * maxi(abs(stereoinl), abs(stereoinr));
	if (peak > *mon)
		*mon = peak;
	else
		*mon += (peak - *mon) * 0.0001f;
}
#else
#define MONITORPEAK(mon, stereoin)
#endif

s16 audioin_is_stereo = 0;
s16 noisegate = 0;
#ifdef DEBUG
// #define NOISETEST
#endif
#ifdef NOISETEST
float noisetestl = 0, noisetestr = 0, noisetest = 0;
#endif
void PreProcessAudioIn(u32* audioin) {
	int newpeak = 0, newpeakr = 0;
#ifdef NOISETEST
	int newpeakl = 0;
#endif
	static float dcl, dcr;
	int ng = mini(256, noisegate);
	// dc remover from audio in, and peak detector while we're there.
	for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
		u32 inp = audioin[i];
		STEREOUNPACK(inp);
		dcl += (inpl - dcl) * 0.0001f;
		dcr += (inpr - dcr) * 0.0001f;
		inpl -= dcl;
		inpr -= dcr;
		newpeakr = maxi(newpeakr, abs(inpr));
#ifdef NOISETEST
		newpeakl = maxi(newpeakl, abs(inpl));
#endif
		if (!audioin_is_stereo)
			inpr = inpl;
		newpeak = maxi(newpeak, abs(inpl + inpr));
		inpl = (inpl * ng) >> 8;
		inpr = (inpr * ng) >> 8;

		audioin[i] = STEREOPACK(inpl, inpr);
	}
	if (newpeak > 400)
		noisegate = 1000;
	else if (noisegate > 0)
		noisegate--;

	if (newpeakr > 300)
		audioin_is_stereo = 1000;
	else if (audioin_is_stereo > 0)
		audioin_is_stereo--;

#ifdef NOISETEST
	if (newpeakl > noisetestl)
		noisetestl = newpeakl;
	else
		noisetestl += (newpeakl - noisetestl) * 0.01f;
	if (newpeakr > noisetestr)
		noisetestr = newpeakr;
	else
		noisetestr += (newpeakr - noisetestr) * 0.01f;
	if (newpeak > noisetest)
		noisetest = newpeak;
	else
		noisetest += (newpeak - noisetest) * 0.01f;
#endif
	int audiorec_gain = (int)(ext_gain_smoother.y2) / 2;

	newpeak = SATURATE16((newpeak * audiorec_gain) >> 14);
	audioin_peak = maxi((audioin_peak * 220) >> 8, newpeak);
	if (audioin_peak > audioin_hold || audioin_holdtime++ > 500) {
		audioin_hold = audioin_peak;
		audioin_holdtime = 0;
	}
}
static s16 scopex = 0;

int audiotime = 0;
void DoAudio(u32* dst, u32* audioin) {
	audiotime += SAMPLES_PER_TICK;
	tc_start(&_tc_audio);
	memset(dst, 0, 4 * SAMPLES_PER_TICK);
	PreProcessAudioIn(audioin); // dc remover; peak detector

	handle_ext_gain();

	// in the process of recording a new sample
	if (sampler_mode > SM_PREVIEW) {
		sampler_recording_tick(dst, audioin);
		return; // skip all other synth functionality
	}

	//////////////////////////////////////////////////////////
	// PLAYMODE

	update_preset_ram(false);
	// a few midi messages per tick. WCGW
	process_all_midi_out();
#ifndef EMU
	PumpWebUSB(true);
	process_serial_midi_in();
#endif
	process_usb_midi_in();

	// do the clock first so we can update the sequencer step etc
	clock_tick();

	seq_tick();

	generate_string_touches();

	arp_tick();

	params_tick();

	update_sample_ram(false);
	update_pattern_ram(false);

	// synth

	handle_synth_voices(dst);

	tc_stop(&_tc_audio);

	spi_tick();

	tc_start(&_tc_fx);

	// delay params

	static u16 delaypos = 0;
	static u32 wetlr;
	const float k_target_fb = param_val_float(P_DLY_FEEDBACK) * (0.35f); // 3/4
	static float k_fb = 0.f;
	int k_target_delaytime = param_val(P_DLY_TIME);
	if (k_target_delaytime > 0) {
		// free timing
		k_target_delaytime = (((k_target_delaytime + 255) >> 8) * k_target_delaytime) >> 8;
		k_target_delaytime = (k_target_delaytime * (DLMASK - 64)) >> 16;
	}
	else {
		k_target_delaytime =
		    sync_divs_32nds[clampi((-k_target_delaytime * 13) >> 16, 0, 12)]; // results in a number 1-32
		// figure out how samples we can have, max, in a beat synced scenario
		int max_delay = (32000 * 600 * 4) / (maxi(150, bpm_10x));
		while (max_delay > DLMASK - 64)
			max_delay >>= 1;
		k_target_delaytime = (max_delay * k_target_delaytime) >> 5;
	}
	k_target_delaytime = clampi(k_target_delaytime, SAMPLES_PER_TICK, DLMASK - 64) << 12;
	int k_delaysend = (param_val(P_DLY_SEND) >> 9);

	static int wobpos = 0;
	static int dwobpos = 0;
	static int wobcount = 0;
	if (wobcount <= 0) {
		const int wobamount = param_val(P_DLY_WOBBLE); // 1/2
		int newwobtarget = ((rand() & 8191) * wobamount) >> 8;
		if (newwobtarget > k_target_delaytime / 2)
			newwobtarget = k_target_delaytime / 2;
		wobcount = ((rand() & 8191) + 8192) & (~(SAMPLES_PER_TICK - 1));
		dwobpos = (newwobtarget - wobpos + wobcount / 2) / wobcount;
	}
	wobcount -= SAMPLES_PER_TICK;

	// hpf params

	static float peak = 0.f;
	peak *= 0.99f;
	static float power = 0.f;
	// at sample rate, lpf k 0.002 takes 10ms to go to half; .0006 takes 40ms; k=.0002 takes 100ms;
	// at buffer rate, k=0.13 goes to half in 10ms; 0.013 goes to half in 100ms; 0.005 is 280ms

	float g = param_val_float(P_HPF); // tanf(3.141592f * 8000.f / 32000.f); // highpass constant // TODO PARAM 0 -1
	g *= g;
	g *= g;
	g += (10.f / 32000.f);
	const static float k = 2.f;
	float a1 = 1.f / (1.f + g * (g + k));
	float a2 = g * a1;

#ifdef DEBUG
#define ENABLE_HPF 0
#else
#define ENABLE_HPF 1
#endif

	if (ENABLE_HPF)
		for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
			u32 input = STEREOSIGMOID(dst[i]);
			STEREOUNPACK(input);
			static float ic1l, ic2l, ic1r, ic2r;
			float l = inputl, r = inputr;
			float v1l = a1 * ic1l + a2 * (l - ic2l);
			float v2l = ic2l + g * v1l;
			ic1l = v1l + v1l - ic1l;
			ic2l = v2l + v2l - ic2l;
			l -= k * v1l + v2l;

			float v1r = a1 * ic1r + a2 * (r - ic2r);
			float v2r = ic2r + g * v1r;
			ic1r = v1r + v1r - ic1r;
			ic2r = v2r + v2r - ic2r;
			r -= k * v1r + v2r;

			power *= 0.999f;
			power += l * l + r * r;
			peak = maxf(peak, l + r);

			s16 li = (s16)SATURATE16(l);
			s16 ri = (s16)SATURATE16(r);
			dst[i] = STEREOPACK(li, ri);
		}

	u32* src = (u32*)dst;

	// reverb params

	float f = 1.f - clampf(param_val_float(P_RVB_TIME), 0.f, 1.f);
	f *= f;
	f *= f;
	k_reverb_fade = (int)(250 * (1.f - f));
	k_reverb_shim = (param_val(P_RVB_SHIMMER) >> 9);
	k_reverb_wob = (param_val_float(P_RVB_WOBBLE));
	// k_reverb_color=(param_val_float(P_RVCOLOR));
	k_reverbsend = (param_val(P_RVB_SEND));

	// mixer params

	int synthlvl_ = param_val(P_SYNTH_LVL);
	int synthwidth = param_val(P_MIX_WIDTH);
	int asynthwidth = abs(synthwidth);
	int synthlvl_mid;
	int synthlvl_side;
	if (asynthwidth <= 32768) { // make more narrow
		synthlvl_mid = synthlvl_;
		synthlvl_side = (synthwidth * synthlvl_) >> 15;
	}
	else {
		synthlvl_side = (synthwidth < 0) ? -synthlvl_ : synthlvl_;
		asynthwidth = 65536 - asynthwidth;
		synthlvl_mid = (asynthwidth * synthlvl_) >> 15;
	}

	int ainwetdry = param_val(P_INPUT_WET_DRY);
	int wetdry = param_val(P_SYNTH_WET_DRY);
	int wetlvl = 65536 - maxi(-wetdry, 0);
	int drylvl = 65536 - maxi(wetdry, 0);

	int a_in_lvl = param_val(P_INPUT_LVL);
	int ainwetlvl = 65536 - maxi(-ainwetdry, 0);
	int aindrylvl = 65536 - maxi(ainwetdry, 0);

	ainwetlvl = ((ainwetlvl >> 4) * (a_in_lvl >> 4)) >> 8;

	a_in_lvl = ((a_in_lvl >> 4) * (drylvl >> 4)) >> 8;    // prescale by dry level
	a_in_lvl = ((a_in_lvl >> 4) * (aindrylvl >> 4)) >> 8; // prescale by fx dry level

	int delayratio = param_val(P_DLY_PINGPONG) >> 8;
	static int delaytime = SAMPLES_PER_TICK << 12;
	int scopescale = (65536 * 24) / maxi(16384, (int)peak);

	// if (g_disable_fx == 1)
	// 	g_disable_fx = 2; // tell the webusb on the main thread we are ready for them
#ifdef DEBUG
#define ENABLE_FX 0
#else
#define ENABLE_FX 1
#endif

	// fx processing

	if (ENABLE_FX /*&& g_disable_fx == 0*/)
		for (int i = 0; i < SAMPLES_PER_TICK / 2; ++i) {

			// delay

			int targetdt = k_target_delaytime + 2048 - (int)wobpos;
			wobpos += dwobpos;
			delaytime += (targetdt - delaytime) >> 10;
			s16 delayreturnl = LINEARINTERPDL(delaybuf, delaypos, delaytime);
			s16 delayreturnr = LINEARINTERPDL(delaybuf, delaypos, ((delaytime >> 4) * delayratio) >> 4);
			// soft clipper due to drive; reduces range to half also giving headroom on tape & output
			u32 drylr0 = STEREOSIGMOID(src[0]);
			u32 drylr1 = STEREOSIGMOID(src[1]);

			// compressor

			u32 drylr01 = STEREOADDAVERAGE(drylr0, drylr1); // this is gonna have absolute max +-32768
			STEREOUNPACK(drylr01);
			static float peaktrack = 1.f;
			float peaky = (float)((1.f / 4096.f / 65536.f)
			                      * (maxi(maxi(drylr01l, -drylr01l), maxi(drylr01r, -drylr01r)) * synthlvl_));
			if (peaky > peaktrack)
				peaktrack += (peaky - peaktrack) * 0.01f;
			else {
				peaktrack += (peaky - peaktrack) * 0.0002f;
				peaktrack = maxf(peaktrack, 1.f);
			}
			float recip = (2.5f / peaktrack);
			int lvl_mid = synthlvl_mid * recip;
			int lvl_side = synthlvl_side * recip;
#ifdef EMU
			m_compressor = synthlvl_ * recip / 65536.f;
#endif
			drylr0 = MIDSIDESCALE(drylr0, lvl_mid, lvl_side);
			drylr1 = MIDSIDESCALE(drylr1, lvl_mid, lvl_side);

			MONITORPEAK(&m_dry, drylr0);
			MONITORPEAK(&m_dry, drylr1);

			u32 ain0 = audioin[i * 2 + 0];
			u32 ain1 = audioin[i * 2 + 1];

			u32 audioinwet = STEREOSCALE(STEREOADDAVERAGE(ain0, ain1), ainwetlvl);
			u32 dry2wetlr = STEREOADDAVERAGE(drylr0, drylr1);
			dry2wetlr = STEREOADDSAT(dry2wetlr, audioinwet);

			MONITORPEAK(&m_dry2wet, dry2wetlr);

			// delay

			int delaysend = (int)((delayreturnl + (delayreturnr >> 1)) * k_fb);
			delaysend += (((s16)(dry2wetlr) + (s16)(dry2wetlr >> 16)) * k_delaysend) >> 8;
			static float lpf = 0.f, dc = 0.f;
			lpf += (delaysend - lpf) * 0.75f;
			dc += (lpf - dc) * 0.05f;
			delaysend = (int)(lpf - dc);
			//- compressor in feedback of delay
			delaysend = MONOSIGMOID(delaysend);

			MONITORPEAK(&m_delaysend, delaysend);

			// adjust feedback up again
			k_fb += (k_target_fb - k_fb) * 0.001f;

			delaypos &= DLMASK;
			delaybuf[delaypos] = delaysend;
			delaypos++;
			s16 li = dry2wetlr;
			s16 ri = dry2wetlr >> 16;
			static s16 prevli = 0;
			static s16 prevprevli = 0;
			static u16 bestedge = 0;
			static s16 antiturningpointli = 0;
			bool turningpoint = (prevli > prevprevli && prevli > li);
			bool antiturningpoint = (prevli < prevprevli && prevli < li);
			if (antiturningpoint)
				antiturningpointli = prevli; // remember the last turning point at the bottom
			if (turningpoint) {              // we are at a peak!
				int edgesize = prevli - antiturningpointli;
				if (scopex >= 256 || (scopex < 0 && edgesize > bestedge)) {
					scopex = -256;
					bestedge = edgesize;
				}
			}
			prevprevli = prevli;
			prevli = li;

			// scope generation

			if (scopex < 256 && scopex >= 0) {
				int x = scopex / 2;
				if (!(scopex & 1))
					clear_scope_pixel(x);
				put_scope_pixel(x, (li * scopescale >> 16) + 16);
				put_scope_pixel(x, (ri * scopescale >> 16) + 16);
			}
			scopex++;
			if (scopex > 1024)
				scopex = -256;

			u32 newwetlr = STEREOPACK(delayreturnl, delayreturnr);
			MONITORPEAK(&m_delayreturn, newwetlr);

#ifndef DEBUG
			if (1) {

				// reverb

				u32 reverbin = STEREOADDAVERAGE(newwetlr, dry2wetlr);
				MONITORPEAK(&m_reverbin, reverbin);
				u32 reverbout = Reverb2(reverbin, reverbbuf);
				MONITORPEAK(&m_reverbout, reverbout);
				newwetlr = STEREOADDSAT(newwetlr, reverbout);
				MONITORPEAK(&m_fxout, newwetlr);
			}
#endif
			// output upsample
			newwetlr = STEREOSCALE(newwetlr, wetlvl);
			u32 midwetlr = STEREOADDAVERAGE(newwetlr, wetlr);
			wetlr = newwetlr;

			u32 audioin0 = STEREOSIGMOID(STEREOSCALE(ain0, a_in_lvl)); // a_in_lvl already scaled by drylvl
			u32 audioin1 = STEREOSIGMOID(STEREOSCALE(ain1, a_in_lvl));
			MONITORPEAK(&m_audioin, audioin0);
			MONITORPEAK(&m_audioin, audioin1);

			// write to output

			src[0] = STEREOADDSAT(STEREOADDSAT(STEREOSCALE(drylr0, drylvl), audioin0), midwetlr);
			src[1] = STEREOADDSAT(STEREOADDSAT(STEREOSCALE(drylr1, drylvl), audioin1), newwetlr);

			MONITORPEAK(&m_output, src[0]);
			MONITORPEAK(&m_output, src[1]);

			src += 2;
		}

#ifdef EMU
	powerout = power / (SAMPLES_PER_TICK * 2.f * 32768.f * 32768.f);
	gainhistoryrms[ghi] = lin2db(powerout + 1.f / 65536.f) * 0.5f;
	ghi = (ghi + 1) & 511;
#endif
	tc_stop(&_tc_fx);
}

/////////////////////////////////////////////////////////

#ifdef EMU
uint32_t emupixels[128 * 32];
void OledFlipEmu(const u8* vram) {
	if (!vram)
		return;
	const u8* src = vram + 1;
	for (int y = 0; y < 32; y += 8) {
		for (int x = 0; x < 128; x++) {
			u8 b = *src++;
			for (int yy = 0; yy < 8; ++yy) {
				u32 c = (b & 1) ? 0xffffffff : 0xff000000;
				int y2 = y + yy;
#ifdef ROTATE_OLED
				emupixels[((31 - y2) + x * 32)] = c; // rotated, pins at top
#else
				emupixels[(y2 * 128 + x)] = c;
#endif
				b >>= 1;
			}
		}
	}
}

int* EMSCRIPTEN_KEEPALIVE getemubitmap(void) {
	return (int*)emupixels;
}
uint8_t* EMSCRIPTEN_KEEPALIVE getemuleds() {
	return (uint8_t*)leds;
}

#endif

void EMSCRIPTEN_KEEPALIVE uitick(u32* dst, const u32* src, int half) {
	tc_stop(&_tc_budget);
	tc_start(&_tc_budget);

	tc_start(&_tc_all);
	//	if (half)
	{
		tc_start(&_tc_touch);
		ui_frame();
		tc_stop(&_tc_touch);
	}
	//	else
	{
		tc_start(&_tc_led);
		leds_update();
		tc_stop(&_tc_led);
	}

	// clear some scope pixels

	// pass thru: memcpy(dst,src,64*4);

	DoAudio((u32*)dst, (u32*)src);
	tc_stop(&_tc_all);

#ifdef RB_SPEEDTEST
	// display _tc_all on oled
	static const u16 log_cycle = 5000;
	static u32 speed_log = 0;
	if (do_every(log_cycle, &speed_log))
		tc_gfx_log(&_tc_all, "all");
	static u32 _tc_all_log = 0;
	if (do_every(log_cycle / 2, &_tc_all_log))
		tc_reset(&_tc_all);
#endif
}

static void jumptobootloader(void) {
	// todo - maybe set a flag in the flash and then use NVIC_SystemReset() which will cause it to jumptobootloader
	// earlier https://community.st.com/s/question/0D50X00009XkeeW/stm32l476rg-jump-to-bootloader-from-software
	typedef void (*pFunction)(void);
	pFunction JumpToApplication;
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	__disable_irq();
	__DSB();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); /* Remap is bot visible at once. Execute some unrelated command! */
	__DSB();
	__ISB();
	JumpToApplication = (void (*)(void))(*((uint32_t*)(0x1FFF0000 + 4)));
	__set_MSP(*(__IO uint32_t*)0x1FFF0000);
	JumpToApplication();
}

void reflash(void) {
	oled_clear();
	draw_str(0, 0, F_16_BOLD, "Re-flash");
	draw_str(0, 16, F_16, "over USB DFU");
	oled_flip();
	HAL_Delay(100);
	jumptobootloader();
}

void set_test_mux(int c) {
#ifndef EMU
	GPIOD->ODR &= ~(GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4); // rgb led off
	if (c & 1)
		GPIOD->ODR |= GPIO_PIN_3;
	if (c & 2)
		GPIOD->ODR |= GPIO_PIN_1;
	if (c & 4)
		GPIOD->ODR |= GPIO_PIN_4;
	if (c & 8)
		GPIOA->ODR |= GPIO_PIN_8;
	else
		GPIOA->ODR &= ~GPIO_PIN_8;
#endif
}
void set_test_rgb(int c) {
	set_test_mux(c ^ 7);
}

short* getrxbuf(void);

#define REVERB_BUF 0x10000000
#define DELAY_BUF 0x20008000

void check_bootloader_flash(void) {
	int count = 0;
	uint32_t* rb32 = (uint32_t*)REVERB_BUF;
	uint32_t magic = rb32[64];
	char* rb = (char*)REVERB_BUF;
	for (; count < 64; ++count)
		if (rb[count] != 1)
			break;
	DebugLog("bootloader left %d ones for us magic is %08x\r\n", count, magic);
	const uint32_t* app_base = (const uint32_t*)DELAY_BUF;

	if (count != 64 / 4 || magic != 0xa738ea75) {
		return;
	}
	char buf[32];
	// checksum!
	uint32_t checksum = 0;
	for (int i = 0; i < 65536 / 4; ++i) {
		checksum = checksum * 23 + ((uint32_t*)DELAY_BUF)[i];
	}
	if (checksum != GOLDEN_CHECKSUM) {
		DebugLog("bootloader checksum failed %08x != %08x\r\n", checksum, GOLDEN_CHECKSUM);
		oled_clear();
		draw_str(0, 0, F_8, "bad bootloader crc");
		snprintf(buf, sizeof(buf), "%08x vs %08x", (unsigned int)checksum, (unsigned int)GOLDEN_CHECKSUM);
		draw_str(0, 8, F_8, buf);
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	oled_clear();
	snprintf(buf, sizeof(buf), "%08x %d", (unsigned int)magic, count);
	draw_str(0, 0, F_16, buf);
	snprintf(buf, sizeof(buf), "%08x %08x", (unsigned int)app_base[0], (unsigned int)app_base[1]);
	draw_str(0, 16, F_12, buf);
	oled_flip();

	rb32[64]++; // clear the magic

	DebugLog("bootloader app base is %08x %08x\r\n", (unsigned int)app_base[0], (unsigned int)app_base[1]);

	/*
	 * We refuse to program the first word of the app until the upload is marked
	 * complete by the host.  So if it's not 0xffffffff, we should try booting it.
	 */
	if (app_base[0] == 0xffffffff || app_base[0] == 0) {
		HAL_Delay(10000);
		return;
	}

	// first word is stack base - needs to be in RAM region and word-aligned
	if ((app_base[0] & 0xff000003) != 0x20000000) {
		HAL_Delay(10000);
		return;
	}

	/*
	 * The second word of the app is the entrypoint; it must point within the
	 * flash area (or we have a bad flash).
	 */
	if (app_base[1] < 0x08000000 || app_base[1] >= 0x08010000) {
		HAL_Delay(10000);
		return;
	}
	DebugLog("FLASHING BOOTLOADER! DO NOT RESET\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "FLASHING\nBOOTLOADER");
	char verbuf[5] = {};
	memcpy(verbuf, (void*)(DELAY_BUF + 65536 - 4), 4);
	draw_str(0, 24, F_8, verbuf);

	oled_flip();
	HAL_FLASH_Unlock();
	FLASH_EraseInitTypeDef EraseInitStruct;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.Page = 0;
	EraseInitStruct.NbPages = 65536 / 2048;
	uint32_t SECTORError = 0;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
		DebugLog("BOOTLOADER flash erase error %d\r\n", SECTORError);
		oled_clear();
		draw_str(0, 0, F_16_BOLD, "BOOTLOADER\nERASE ERROR");
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	DebugLog("BOOTLOADER flash erased ok!\r\n");

	__HAL_FLASH_DATA_CACHE_DISABLE();
	__HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
	__HAL_FLASH_DATA_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
	__HAL_FLASH_DATA_CACHE_ENABLE();
	uint64_t* s = (uint64_t*)DELAY_BUF;
	volatile uint64_t* d = (volatile uint64_t*)0x08000000;
	u32 size_bytes = 65536;
	for (; size_bytes > 0; size_bytes -= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (uint32_t)(size_t)(d++), *s++);
	}
	HAL_FLASH_Lock();
	DebugLog("BOOTLOADER has been flashed!\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "BOOTLOADER\nFLASHED OK!");
	draw_str(0, 24, F_8, verbuf);
	oled_flip();
	HAL_Delay(3000);
}

#undef ERROR
#define ERROR(msg, ...)                                                                                                \
	do {                                                                                                               \
		errorcount++;                                                                                                  \
		DebugLog("\r\n" msg "\r\n", __VA_ARGS__);                                                                      \
	} while (0)

void test_jig(void) {
	// pogo pin layout:
	// GND DEBUG = GND / PA8 - 67
	// GND GND
	// MISO SPICLK = PD3 - 84 / PD1 - 82
	// MOSI GND = PD4 - 85 / GND
	// rgb led is hooked to PD1,PD3,PD4. configure it for output
	// mux address hooked to LSB=PD3, PD1, PD4, PA8=MSB
#ifndef EMU
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// we also use debug as an output now!
	GPIO_InitStruct.Pin = GPIO_PIN_8;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	GPIOA->ODR &= ~GPIO_PIN_8;
#endif
	oled_clear();
	draw_str(0, 0, F_32_BOLD, "TEST JIG");
	oled_flip();
	HAL_Delay(100);
	send_cv_trigger(false);
	send_cv_clock(false);
	send_cv_gate(0);
	send_cv_pressure(0);
	int gndcalib[ADC_CHANS] = {0};
	int refcalib[ADC_CHANS] = {0};
	float pdac[2][2] = {0};
#define PITCH_1V_OUT (43000 - 8500) // about 8500 per volt; 43000 is zero ish.
#define PITCH_4V_OUT (43000 - 8500 * 4)
	static int const expected_mvolts[11][2] = {
	    {0, 0},       // gnd
	    {2500, 2500}, // 2.5 ref
	    {3274, 3274}, // 3.3 supply
	    {4779, 4779}, // 5v supply
	    {950, 950},   // 1v from 12v supply
	    {1039, 4230}, // pitch lo 1v/4v
	    {1039, 4230}, // pitch hi 1v/4v
	    {0, 4700},    // clock
	    {0, 4700},    // trig,
	    {0, 4700},    // gate,
	    {0, 4700},    // pressure
	};
	static int const tol_mvolts[11] = {
	    100,                // gnd
	    10,                 // ref
	    300,                // 3.3
	    500,                // 5
	    100,                // 1v
	    100, 100,           // pitch
	    150, 150, 150, 150, // outputs
	};
	// const char* const names[11][2] = {{"gnd", 0},
	//                                   {"2.5v", 0},
	//                                   {"3.3v", 0},
	//                                   {"5v", 0},
	//                                   {"1v from 12v", 0},
	//                                   {"plo (1v)", "plo (4v)"},
	//                                   {"phi (1v)", "phi (4v)"},
	//                                   {"clk (0v)", "clk (4.6v)"},
	//                                   {"trig (0v)", "trig (4.6v)"},
	//                                   {"gate (0v)", "gate (4.6v)"},
	//                                   {"pressure (0v)", "pressure (4.6v)"}};
	while (1) {
		DebugLog("mux in:  pitch     gate      x        y        a        b   | mux:\r\n");
		int errorcount = 0;
		int rangeok = 0, zerook = 0;
		// reset_ext_clock();
		for (int iter = 0; iter < 4; ++iter) {
			send_cv_clock(false);
			HAL_Delay(3);
			send_cv_clock(true);
			HAL_Delay(3);
		}
		// if (ext_clock_counter != 4)
		// 	ERROR("expected clkin of 4, got %d", ext_clock_counter);
		for (int mux = 0; mux < 11; ++mux) {
			set_test_mux(mux);
			int numlohi = (mux < 5) ? 1 : 2;
			for (int lohi = 0; lohi < numlohi; ++lohi) {
				int data = lohi ? 49152 : 0;
				int pitch = lohi ? PITCH_4V_OUT : PITCH_1V_OUT;
				send_cv_trigger(data > 0 ? true : false);
				send_cv_clock(data > 0 ? true : false);
				send_cv_gate(data);
				send_cv_pressure(data);
				send_cv_pitch_lo(pitch, 0);
				send_cv_pitch_hi(pitch, 0);

				HAL_Delay(3);
				int tot[ADC_CHANS] = {0};
				oled_clear();

#define NUMITER 32
				for (int iter = 0; iter < NUMITER; ++iter) {
					HAL_Delay(2);
					short* rx = getrxbuf();
					for (int x = 0; x < 128; ++x) {
						put_pixel(x, 16 + rx[x * 2] / 1024, 1);
						put_pixel(x, 16 + rx[x * 2 + 1] / 1024, 1);
					}
					for (int j = 0; j < ADC_SAMPLES; ++j)
						for (int ch = 0; ch < ADC_CHANS; ++ch)
							tot[ch] += adc_buffer[j * ADC_CHANS + ch];
				}
				if (lohi)
					inverted_rectangle(0, 0, 128, 32);
				oled_flip();
				for (int ch = 0; ch < ADC_CHANS; ++ch) {
					tot[ch] /= ADC_SAMPLES * NUMITER;
				}
				DebugLog("-----\nmux = %d lohi = %d\n", mux, lohi);
				for (int ch = 0; ch < ADC_CHANS; ++ch) {
					DebugLog("adc ch reads %d\n", tot[ch]);
				}
				DebugLog("-----\n");
				switch (mux) {
				case 0:
					for (int ch = 0; ch < ADC_CHANS; ++ch) {
						gndcalib[ch] = tot[ch];
						int expected = (ch == 0) ? 43262 : (ch >= 6) ? 31500 : 31035;
						int error = abs(expected - tot[ch]);
						if (error > 2000)
							ERROR("ADC Channel %d zero point is %d, expected %d", ch, tot[ch], expected);
						else
							zerook |= (1 << ch);
					}
					break;
				case 1:
					for (int ch = 0; ch < ADC_CHANS; ++ch) {
						refcalib[ch] = tot[ch];
						int range = gndcalib[ch] - refcalib[ch];
						int expected = (ch == 0) ? 21600 : (ch >= 6) ? 0 : 14386;
						int error = abs(expected - range);
						if (error > 2000)
							ERROR("ADC Channel %d range is %d, expected %d", ch, range, expected);
						else
							rangeok |= (1 << ch);
					}
					break;
				case 5:
				case 6: {
					int range0 = (refcalib[0] - gndcalib[0]);
					if (range0 == 0)
						range0 = 1;
					pdac[mux - 5][lohi] = (tot[0] - gndcalib[0]) * (2.5f / range0);
					break;
				}
				}
				DebugLog("%4d: ", mux);
				for (int ch = 0; ch < 6; ++ch) {
					int range = (refcalib[ch] - gndcalib[ch]);
					if (range == 0)
						range = 1;
					float gain = 2500.f / range;
					int mvolts = (tot[ch] - gndcalib[ch]) * gain;
					int exp_mvolts = expected_mvolts[mux][lohi];
					int error = abs(exp_mvolts - mvolts);
					int tol = tol_mvolts[mux];
					bool ok = true;
					if (error > tol) {
						ok = false;
						ERROR("ADC channel %d was %dmv, expected %dmv, outside tolerence of %dmv", ch, mvolts,
						      exp_mvolts, tol);
					}
					DebugLog("%6dmv%c ", mvolts, ok ? ' ' : '*');
				}
				// rj: logging should be re-implemented with custom clock counter
				// DebugLog("| %s. clocks=%d\r\n", names[mux][lohi], ext_clock_counter);
			}
		}
		DebugLog("zero: ");
		for (int ch = 0; ch < 8; ++ch)
			DebugLog("%6d%c   ", gndcalib[ch], (zerook & (1 << ch)) ? ' ' : '*');
		DebugLog("\r\nrange ");
		for (int ch = 0; ch < 6; ++ch)
			DebugLog("%6d%c   ", gndcalib[ch] - refcalib[ch], (rangeok & (1 << ch)) ? ' ' : '*');
		DebugLog("\r\n%d errors\r\n\r\n", errorcount);
		set_test_rgb(errorcount ? 4 : 2);
		oled_clear();
		if (errorcount == 0)
			draw_str(0, 0, F_32_BOLD, "GOOD!");
		else
			fdraw_str(0, 0, F_32_BOLD, "%d ERRORS", errorcount);
		oled_flip();
		for (int ch = 0; ch < 8; ++ch) {
			int zero = gndcalib[ch];
			int range = gndcalib[ch] - refcalib[ch];
			if (range == 0)
				range = 1;
			adc_dac_calib[ch].bias = zero;
			if (ch >= 6)
				adc_dac_calib[ch].scale = -1.01f / (zero + 1);
			else if (ch == 0)
				adc_dac_calib[ch].scale =
				    -2.5f / range; // range is measured off 2.5; so this scales it so that we get true volts out
			else
				adc_dac_calib[ch].scale =
				    (-2.5f / 5.f)
				    / range; // range is measured off 2.5; so this scales it so that we get 1 out for 5v in
		}
		// ok pdac[k][0] tells us what we got from the ADC when we set the DAC to PITCH_1V_OUT, and pdac[k][1] tells
		// us what we got when we output PITCH_4V_OUT so we have dacb + dacs * plo0 = PITCH_1V_OUT and       dacb +
		// dacs * plo1 = PITCH_4V_OUT dacs = (PITCH_1V_OUT-PITCH_4V_OUT) / (plo0-plo1) dacb = PITCH_1V_OUT -
		// dacs*plo0
		for (int dacch = 0; dacch < 2; ++dacch) {
			float range = (pdac[dacch][0] - pdac[dacch][1]);
			if (range == 0)
				range = 1.f;
			float scale_per_volt = (PITCH_1V_OUT - PITCH_4V_OUT) / range;
			float zero = PITCH_1V_OUT - scale_per_volt * pdac[dacch][0];
			DebugLog("dac channel %d has zero at %d and %d steps per volt, should be around 42500 and -8000 ish\r\n",
			         dacch, (int)zero, (int)scale_per_volt);
			adc_dac_calib[dacch + 8].bias = zero;
			adc_dac_calib[dacch + 8].scale = scale_per_volt * (1.f / (2048.f * 12.f)); // 2048 per semitone
		}

		flash_writecalib(2);

		HAL_Delay(errorcount ? 2000 : 4000);
	}
}

void plinky_frame(void);

// #define BITBANG
#ifdef WASM

bool send_midimsg(u8 status, u8 data1, u8 data2) {
	return true;
}
void spi_update_dac(int chan) {
	resetspistate();
}

void EmuStartSound(void) {
}

bool usb_midi_receive(unsigned char packet[4]) {
	return false; // fill in packet and return true if midi found
}
int emutouch[9][2];
void EMSCRIPTEN_KEEPALIVE wasm_settouch(int idx, int pos, int pressure) {
	if (idx >= 0 && idx < 9)
		emutouch[idx][1] = pos, emutouch[idx][0] = pressure;
}

void EMSCRIPTEN_KEEPALIVE plinky_frame_wasm(void) {
	plinky_frame();
}
u32 wasmbuf[SAMPLES_PER_TICK];
uint8_t* EMSCRIPTEN_KEEPALIVE get_wasm_audio_buf(void) {
	return (uint8_t*)wasmbuf;
}
uint8_t* EMSCRIPTEN_KEEPALIVE get_wasm_preset_buf(void) {
	return (uint8_t*)&cur_preset;
}
void EMSCRIPTEN_KEEPALIVE wasm_audio(void) {
	static u8 half = 0;
	u32 audioin[SAMPLES_PER_TICK] = {0};
	uitick(wasmbuf, audioin, half);
	half = 1 - half;
}
void EMSCRIPTEN_KEEPALIVE wasm_pokepreset(int offset, int byte) {
	if (offset >= 0 && offset < sizeof(cur_preset))
		((u8*)&cur_preset)[offset] = byte;
}
int EMSCRIPTEN_KEEPALIVE wasm_peekpreset(int offset) {
	if (offset >= 0 && offset < sizeof(cur_preset))
		return ((u8*)&cur_preset)[offset];
	return 0;
}
int EMSCRIPTEN_KEEPALIVE wasm_getcurpreset(void) {
	return sys_params.curpreset;
}
void EMSCRIPTEN_KEEPALIVE wasm_setcurpreset(int i) {
	load_preset(i, false);
}
#endif

void EMSCRIPTEN_KEEPALIVE plinky_init(void) {
	accel_init();
	denormals_init();
	reset_touches();
	tc_init();

	HAL_Delay(100); // stablise power before bringing oled up
	gfx_init();     // also initializes oled
	check_bootloader_flash();
	reverb_clear(); // ram2 is not cleared by startup.s as written.
	delay_clear();
	codec_init();
	adc_dac_init();

#ifdef EMU
	void EmuStartSound(void);
	EmuStartSound();
#endif

	// see if were in the testjig - it pulls PA8 (pin 67) down 'DEBUG'
#ifndef EMU
	if (!(GPIOA->IDR & (1 << 8))) {
		test_jig();
	}

	// turn debug pin to an output
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_8;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	GPIOA->BSRR = 1 << 8; // cs high
	HAL_Delay(1);
	spi_init();
	midi_init();

#endif
	leds_init();

	int flashvalid = flash_readcalib();
	if (!(flashvalid & 1)) { // no calib at all
		reset_touches();
		calib();
		flashvalid |= 1;
		flash_writecalib(flashvalid);
	}
	if (!(flashvalid & 2)) {
		// cv_reset_calib();
		cv_calib();
		flashvalid |= 2;
		flash_writecalib(3);
	}
	HAL_Delay(80);
	int knoba = adc_get_raw(ADC_A_KNOB);
	int knobb = adc_get_raw(ADC_B_KNOB);
	leds_bootswish();
	knoba = abs(knoba - (int)adc_get_raw(ADC_A_KNOB));
	knobb = abs(knobb - (int)adc_get_raw(ADC_B_KNOB));
	DebugLog("knob turned by %d,%d during boot\r\n", knoba, knobb);
	//  turn knobs during boot to force calibration
#ifndef WASM
	if (knoba > 4096 || knobb > 4096) {
		if (knoba > 4096 && knobb > 4096) {
			// both knobs twist on boot - jump to stm flash bootloader
			reflash();
		}
		if (knoba > 4096) {
			// left knob twist on boot - full calib
			reset_touches();
			calib();
		}
		else {
			// right knob twist on boot - cv calib only
			cv_calib();
		}
		flash_writecalib(3);
	}
#endif
	init_flash();
	init_ram();

	sampler_mode = SM_PREVIEW;
}

#include "ui.h"
