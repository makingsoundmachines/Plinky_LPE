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