/*
 * core.h
 *
 *  Created on: 24 Oct 2019
 *      Author: mmalex
 */

#ifndef SRC_CORE_H_
#define SRC_CORE_H_

#include "utils.h"

#include "config.h"
#ifdef EMU
#include <stdint.h>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
static inline void HAL_Delay(int ms) {
	Sleep(ms);
}
#else // wasm?
static inline void HAL_Delay(int ms) {
}
#endif
#endif

#ifdef __APPLE__
#include <unistd.h>
#define _write write
#endif

#define PI 3.141592653589793f

void DebugLog(const char* fmt, ...);
#ifndef EMU
#define EmuDebugLog(...)
#else
#define EmuDebugLog DebugLog
#endif

#ifndef EMU
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

static inline void denormals_init(void) {
	// no denormals
#ifndef EMU
	uint32_t fcspr = __get_FPSCR();
	__set_FPSCR(fcspr | (1 << 24));
#endif
}

void DebugLog(const char* fmt, ...);

#ifdef EMU
#ifdef _WIN32
LARGE_INTEGER pffreq, pfstart;
static inline uint64_t emu_rdtsc(void) {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (((now.QuadPart - pfstart.QuadPart) * 80000000) / pffreq.QuadPart);
}
#define RDTSC() emu_rdtsc()
static inline u32 millis(void) {
	return RDTSC() / 80000;
}
static inline u32 micros(void) {
	return RDTSC() / 80;
}
static inline void tc_init(void) {
	QueryPerformanceFrequency(&pffreq);
	QueryPerformanceCounter(&pfstart);
}
#elif defined(__APPLE__)
#include <mach/mach_time.h>
uint64_t time_start;
mach_timebase_info_data_t timebase_info;
static inline uint64_t emu_rdtsc(void) {
	uint64_t time_now = mach_absolute_time();
	uint64_t time_elapsed = time_now - time_start;
	return (time_elapsed * timebase_info.numer * 80) / (1000 * timebase_info.denom);
}
#define RDTSC() emu_rdtsc()
static inline u32 millis(void) {
	return RDTSC() / 80000;
}
static inline u32 micros(void) {
	return RDTSC() / 80;
}
static inline void tc_init(void) {
	mach_timebase_info(&timebase_info);
	time_start = mach_absolute_time();
}
#else // wasm
int _millis;
#define RDTSC() (0)
static inline u32 millis(void) {
	return _millis;
}
static inline u32 micros(void) {
	return _millis * 1000;
}
static inline void tc_init(void) {
}
#endif
#endif

#ifdef IMPL

void DebugLog(const char* fmt, ...) {
#if defined DEBUG || defined _DEBUG
	static bool logging = false;
	if (logging)
		return;
	logging = true;
#ifndef EMU
	while (huart3.gState != HAL_UART_STATE_READY)
		;
#endif
#ifdef WASM
	va_list args;
	va_start(args, fmt);
	int n = vprintf(fmt, args);
#else
	char buf[256];
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
#ifdef EMU
	_write(1, buf, n);
#else
	// HAL_UART_Transmit_IT(&huart3, (u8*) buf, n);
	HAL_UART_Transmit(&huart3, (u8*)buf, n, 1000);
#endif
#endif
	logging = false;
#endif
}
#endif

#endif /* SRC_CORE_H_ */
