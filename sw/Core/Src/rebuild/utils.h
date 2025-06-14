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

// TEMP - all of these will get organised into their appropriate modules

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

enum {            // these are sequencer modes
	PLAY_STOPPED, // not playing
	PLAY_PREVIEW, // default mode after pressing play, if you release it quickly (short-press) the mode turns into
	              // PLAYING and continues on. during play_preview, the sequencer only previews the current step
	PLAY_WAITING_FOR_CLOCK_START, // this is never triggered
	PLAY_WAITING_FOR_CLOCK_STOP,  // the sequencer is finishing the step an will stop afterwards
	PLAYING,                      // playing
};

typedef struct knobsmoother {
	float y1, y2;
} knobsmoother;

typedef struct SysParams {
	u8 curpreset;
	u8 paddy;
	u8 systemflags;
	s8 headphonevol;
	u8 pad[16 - 4];
} SysParams;

enum EPages {
	PG_SOUND1,
	PG_SOUND2,
	PG_ENV1,
	PG_ENV2,
	PG_DELAY,
	PG_REVERB,
	PG_ARP,
	PG_SEQ,
	PG_SAMPLER,
	PG_JITTER,
	PG_A,
	PG_B,
	PG_X,
	PG_Y,
	PG_MIX1,
	PG_MIX2,
	PG_LAST,
};

enum EParams {

	P_PWM = PG_SOUND1 * 6,
	P_DRIVE,
	P_PITCH,
	P_OCT,
	P_GLIDE,
	P_INTERVAL,

	P_NOISE = PG_SOUND2 * 6,
	P_MIXRESO,
	P_ROTATE,
	P_SCALE,
	P_MICROTUNE,
	P_STRIDE,

	P_SENS = PG_ENV1 * 6,
	P_A,
	P_D,
	P_S,
	P_R,
	P_ENV1_UNUSED,

	P_ENV_LEVEL = PG_ENV2 * 6,
	P_A2,
	P_D2,
	P_S2,
	P_R2,
	P_ENV2_UNUSED,

	P_DLSEND = PG_DELAY * 6,
	P_DLTIME,
	P_DLRATIO,
	P_DLWOB,
	P_DLFB,
	P_TEMPO,

	P_RVSEND = PG_REVERB * 6,
	P_RVTIME,
	P_RVSHIM,
	P_RVWOB,
	P_RVUNUSED,
	P_SWING,

	P_ARPONOFF = PG_ARP * 6,
	P_ARPMODE,
	P_ARPDIV,
	P_ARPPROB,
	P_ARPLEN,
	P_ARPOCT,

	P_LATCHONOFF = PG_SEQ * 6,
	P_SEQMODE,
	P_SEQDIV,
	P_SEQPROB,
	P_SEQLEN,
	P_GATE_LENGTH,

	P_SMP_POS = PG_SAMPLER * 6,
	P_SMP_GRAINSIZE,
	P_SMP_RATE,
	P_SMP_TIME,
	P_SAMPLE,
	P_SEQPAT,

	P_JIT_POS = PG_JITTER * 6,
	P_JIT_GRAINSIZE,
	P_JIT_RATE,
	P_JIT_PULSE, // TODO
	P_JIT_UNUSED,
	P_SEQSTEP,

	P_ASCALE = PG_A * 6,
	P_AOFFSET,
	P_ADEPTH,
	P_AFREQ,
	P_ASHAPE,
	P_AWARP,

	P_BSCALE = PG_B * 6,
	P_BOFFSET,
	P_BDEPTH,
	P_BFREQ,
	P_BSHAPE,
	P_BWARP,

	P_XSCALE = PG_X * 6,
	P_XOFFSET,
	P_XDEPTH,
	P_XFREQ,
	P_XSHAPE,
	P_XWARP,

	P_YSCALE = PG_Y * 6,
	P_YOFFSET,
	P_YDEPTH,
	P_YFREQ,
	P_YSHAPE,
	P_YWARP,

	P_MIXSYNTH = PG_MIX1 * 6,
	P_MIXWETDRY,
	P_MIXHPF,
	P_MIDI_CH_IN, // P_MIX_UNUSED,
	P_CV_QUANT,
	P_HEADPHONE, // system?

	P_MIXINPUT = PG_MIX2 * 6,
	P_MIXINWETDRY,
	P_SYS_UNUSED1,
	P_MIDI_CH_OUT, // P_SYS_UNUSED2,
	P_ACCEL_SENS,
	P_MIX_WIDTH,

	P_LAST = PG_LAST * 6,
};

enum { GEN_PRESET, GEN_PAT0, GEN_PAT1, GEN_PAT2, GEN_PAT3, GEN_SYS, GEN_SAMPLE, GEN_LAST };

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

typedef struct FingerRecord {
	u8 pos[4];
	u8 pres[8];
} FingerRecord;

typedef struct PatternQuarter {
	FingerRecord steps[16][8];
	s8 autoknob[16 * 8][2];
} PatternQuarter;
// static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
// static_assert((sizeof(PatternQuarter) & 15) == 0, "?");

#define MAX_SAMPLE_LEN (1024 * 1024 * 2) // max sample length in samples

// - TEMP

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

// debug
void gfx_debug(u8 row, const char* fmt, ...);