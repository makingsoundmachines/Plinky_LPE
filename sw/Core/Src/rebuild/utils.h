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

// basic typedefs
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#ifndef __cplusplus
typedef char bool;
#define true 1
#define false 0
#endif

// basic defines
#define NUM_STRINGS 8

// custom typedefs

typedef struct Touch {
	s16 pres;
	u16 pos;
} Touch;

typedef struct CalibResult {
	u16 pres[NUM_STRINGS];
	s16 pos[NUM_STRINGS];
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
// modulo that accounts for negative x values
static inline u32 modi(s32 x, u32 y) {
	s32 m = x % y;
	return (m < 0) ? m + y : m;
}
static inline bool ispow2(s16 x) {
	return (x & (x - 1)) == 0;
}

// debug
void gfx_debug(u8 row, const char* fmt, ...);
void DebugLog(const char* fmt, ...);

// plinky utils
static u8 const zero[2048] = {0};

static inline float deadzone(float f, float zone) {
	if (f < zone && f > -zone)
		return 0.f;
	if (f > 0.f)
		f -= zone;
	else
		f += zone;
	return f;
}

static inline void set_smoother(ValueSmoother* s, float new_val) {
	s->y1 = s->y2 = new_val;
}

static inline void smooth_value(ValueSmoother* s, float new_val, float max_scale) {
	// inspired by  https ://cytomic.com/files/dsp/DynamicSmoothing.pdf
	float band = fabsf(s->y2 - s->y1);
	float sens = 8.f / max_scale;
	float g = minf(1.f, 0.05f + band * sens);
	s->y1 += (new_val - s->y1) * g;
	s->y2 += (s->y1 - s->y2) * g;
}

static inline int rand_range(int mn, int mx) {
	return mn + (((rand() & 255) * (mx - mn)) >> 8);
}
static inline u8 pres_compress(s16 pressure) {
	return clampi((pressure + 12) / 24, 0, 255);
}
static inline u16 pres_decompress(u8 pressure) {
	return maxi(rand_range(24 * pressure - 12, 24 * pressure + 12), 0);
}
static inline u8 pos_compress(u16 position) {
	return clampi((position + 4) / 8, 0, 255);
}
static inline u16 pos_decompress(u8 position) {
	return maxi(rand_range(8 * position - 4, 8 * position + 4), 0);
}

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

// TEMP - these will get organised into their appropriate modules
// save/load
typedef struct Preset {
	s16 params[96][8];
	u8 flags;
	u8 seq_start;
	u8 seq_len;
	u8 paddy[3];
	u8 version;
	u8 category;
	u8 name[8];
} Preset;
// static_assert((sizeof(Preset) & 15) == 0, "?");
// static_assert(sizeof(Preset) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");

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

#include "low_level/audiointrin.h"

#define NUM_KNOBS 2
#define NUM_QUARTERS 4

#define EXPANDER_ZERO 0x800
#define EXPANDER_RANGE 0x7ff
#define EXPANDER_MAX 0xfff
#define EXPANDER_GAIN 0.715f
#define MAX_PTN_STEPS 64
#define PTN_STEPS_PER_QTR (MAX_PTN_STEPS / NUM_QUARTERS)
#define PTN_SUBSTEPS 8

#define I2C_TIMEOUT 20

typedef struct PatternStringStep {
	u8 pos[PTN_SUBSTEPS / 2];
	u8 pres[PTN_SUBSTEPS];
} PatternStringStep;

typedef struct PatternQuarter {
	PatternStringStep steps[PTN_STEPS_PER_QTR][NUM_STRINGS];
	s8 autoknob[PTN_STEPS_PER_QTR * PTN_SUBSTEPS][NUM_KNOBS];
} PatternQuarter;
// these should be incorporated in memory module:
// static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
// static_assert((sizeof(PatternQuarter) & 15) == 0, "?");

#define CUR_PRESET_VERSION 2

#define NUM_PRESETS 32
#define NUM_PATTERNS 24
#define NUM_SAMPLES 8

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