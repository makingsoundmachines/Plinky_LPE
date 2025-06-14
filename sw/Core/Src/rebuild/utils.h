#pragma once

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

typedef struct knobsmoother {
	float y1, y2;
} knobsmoother;

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

// TEMP - these will get organised into their appropriate modules

#define FULL 1024
#define HALF (FULL / 2)

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

#define MAX_SAMPLE_LEN (1024 * 1024 * 2) // max sample length in samples
#define BLOCK_SAMPLES 64