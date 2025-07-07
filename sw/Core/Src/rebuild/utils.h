#pragma once

// this makes sure the editor correctly recognizes
// which parts of the STM libraries we have access to
#ifndef STM32L476xx
#define STM32L476xx
#endif

// core libraries
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stm32 libraries
#include "stm32l476xx.h"
#include "stm32l4xx_hal.h"

// data
#include "data/tables.h"

// basic typedefs
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#ifndef __cplusplus
typedef char bool;
#define true 1
#define false 0
#endif

// custom typedefs

typedef struct Touch {
	s16 pres;
	u16 pos;
} Touch;

typedef struct CalibResult {
	u16 pres[8];
	s16 pos[8];
} CalibResult;

typedef struct ValueSmoother {
	float y1, y2;
} ValueSmoother;

// time
#define RDTSC() (DWT->CYCCNT)
static inline u32 millis(void) {
	return HAL_GetTick();
}
static inline u32 micros(void) {
	return TIM5->CNT;
}
// returns true every [duration] ms
static inline bool do_every(u32 duration, u32* referenceTime) {
	if (millis() - *referenceTime >= duration) {
		*referenceTime = millis();
		return true;
	}
	return false;
}

// utils
static inline int mini(int a, int b) {
	return (a < b) ? a : b;
}
static inline int maxi(int a, int b) {
	return (a > b) ? a : b;
}
static inline float minf(float a, float b) {
	return (a < b) ? a : b;
}
static inline float maxf(float a, float b) {
	return (a > b) ? a : b;
}
static inline int clampi(int x, int a, int b) {
	return mini(maxi(x, a), b);
}
static inline float clampf(float x, float a, float b) {
	return minf(maxf(x, a), b);
}
static inline float squaref(float x) {
	return x * x;
}
static inline float lerp(float a, float b, float t) {
	return a + (b - a) * t;
}
static inline u8 triangle(u8 x) {
	return (x < 128) ? x * 2 : (511 - x * 2);
}
static inline bool ispow2(s16 x) {
	return (x & (x - 1)) == 0;
}

// debug
void gfx_debug(u8 row, const char* fmt, ...);
void DebugLog(const char* fmt, ...);

// plinky utils
static inline float deadzone(float f, float zone) {
	if (f < zone && f > -zone)
		return 0.f;
	if (f > 0.f)
		f -= zone;
	else
		f += zone;
	return f;
}

static inline float smooth_value(ValueSmoother* s, float new_val, float max_scale) {
	// inspired by  https ://cytomic.com/files/dsp/DynamicSmoothing.pdf
	float band = fabsf(s->y2 - s->y1);
	float sens = 8.f / max_scale;
	float g = minf(1.f, 0.05f + band * sens);
	s->y1 += (new_val - s->y1) * g;
	s->y2 += (s->y1 - s->y2) * g;
	return s->y2;
}

// TEMP - these will get organised into their appropriate modules

#define FULL 1024
#define HALF (FULL / 2)

// clang-format off
#define SWAP(a,b) if (a>b) { int t=a; a=b; b=t; }
static inline void sort8(int *dst, const int *src) {
	int a0=src[0],a1=src[1],a2=src[2],a3=src[3],a4=src[4],a5=src[5],a6=src[6],a7=src[7];
	SWAP(a0,a1);SWAP(a2,a3);SWAP(a4,a5);SWAP(a6,a7);
	SWAP(a0,a2);SWAP(a1,a3);SWAP(a4,a6);SWAP(a5,a7);
	SWAP(a1,a2);SWAP(a5,a6);SWAP(a0,a4);SWAP(a3,a7);
	SWAP(a1,a5);SWAP(a2,a6);
	SWAP(a1,a4);SWAP(a3,a6);
	SWAP(a2,a4);SWAP(a3,a5);
	SWAP(a3,a4);
	dst[0]=a0; dst[1]=a1; dst[2]=a2; dst[3]=a3; dst[4]=a4; dst[5]=a5; dst[6]=a6; dst[7]=a7;
}
#undef SWAP
// clang-format on

typedef struct euclid_state {
	int trigcount;
	bool did_a_retrig;
	bool supress;

} euclid_state;

// save/load
typedef struct Preset {
	s16 params[96][8];
	u8 flags;
	s8 loopstart_step_no_offset;
	s8 looplen_step;
	u8 paddy[3];
	u8 version;
	u8 category;
	u8 name[8];
} Preset;
// static_assert((sizeof(Preset) & 15) == 0, "?");
// static_assert(sizeof(Preset) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");

// audio stuff
enum {
	EA_OFF = 0,
	EA_PASSTHRU = -1,
	EA_PLAY = 1,
	EA_PREERASE = 2,
	EA_MONITOR_LEVEL = 3,
	EA_ARMED = 4,
	EA_RECORDING = 5,
	EA_STOPPING1 = 6, // we stop for 4 cycles to write 0s at the end
	EA_STOPPING2 = 7,
	EA_STOPPING3 = 8,
	EA_STOPPING4 = 9,
};

// these are sequencer modes
enum {
	PLAY_STOPPED, // not playing
	PLAY_PREVIEW, // default mode after pressing play, if you release it quickly (short-press) the mode turns into
	              // PLAYING and continues on. during play_preview, the sequencer only previews the current step
	PLAY_WAITING_FOR_CLOCK_START, // this is never triggered
	PLAY_WAITING_FOR_CLOCK_STOP,  // the sequencer is finishing the step an will stop afterwards
	PLAYING,                      // playing
};

// save/load
typedef struct SysParams {
	u8 curpreset;
	u8 paddy;
	u8 systemflags;
	s8 headphonevol;
	u8 pad[16 - 4];
} SysParams;

// save/load
enum { GEN_PRESET, GEN_PAT0, GEN_PAT1, GEN_PAT2, GEN_PAT3, GEN_SYS, GEN_SAMPLE, GEN_LAST };

// sampler
typedef struct SampleInfo {
	u8 waveform4_b[1024]; // 4 bits x 2048 points, every 1024 samples
	int splitpoints[8];
	int samplelen; // must be after splitpoints, so that splitpoints[8] is always the length.
	s8 notes[8];
	u8 pitched;
	u8 loop; // bottom bit: loop; next bit: slice vs all
	u8 paddy[2];
} SampleInfo;
// static_assert(sizeof(SampleInfo) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
// static_assert((sizeof(SampleInfo) & 15) == 0, "?");

typedef struct FingerRecord { // sequencer
	u8 pos[4];
	u8 pres[8];
} FingerRecord;

typedef struct PatternQuarter { // sequecer
	FingerRecord steps[16][8];
	s8 autoknob[16 * 8][2];
} PatternQuarter;
// static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
// static_assert((sizeof(PatternQuarter) & 15) == 0, "?");

enum {
	FLAGS_ARP = 1,
	FLAGS_LATCH = 2,
};

typedef struct Osc {
	u32 phase, prevsample;
	s32 dphase;
	s32 targetdphase;
	int pitch;
} Osc;

typedef struct GrainPair {
	int fpos24;
	int pos[2];
	int vol24;
	int dvol24;
	int dpos24;
	float grate_ratio;
	float multisample_grate;
	int bufadjust; // for reverse grains, we adjust the dma buffer address by this many samples
	int outflags;
} GrainPair;

typedef struct Voice {
	float vol;
	float y[4];
	Osc theosc[4];
	GrainPair thegrains[2];
	// grain synth state
	int playhead8;
	u8 sliceidx;
	int initialfingerpos;
	ValueSmoother fingerpos;

	u8 decaying;
	int env_cur16;
	float noise;
	float env_level;
	int env_decaying;
} Voice;

#define MAX_SAMPLE_LEN (1024 * 1024 * 2) // max sample length in samples
#define BLOCK_SAMPLES 64
#define FLAG_MASK 127