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