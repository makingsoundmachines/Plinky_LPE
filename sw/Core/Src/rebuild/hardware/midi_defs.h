#pragma once
#include "synth/params.h"
#include "utils.h"

typedef enum MidiMessageType {
	// Channel Voice Messages
	MIDI_NOTE_OFF = 0x80,          // 1000 0000
	MIDI_NOTE_ON = 0x90,           // 1001 0000
	MIDI_POLY_KEY_PRESSURE = 0xA0, // 1010 0000
	MIDI_CONTROL_CHANGE = 0xB0,    // 1011 0000
	MIDI_PROGRAM_CHANGE = 0xC0,    // 1100 0000
	MIDI_CHANNEL_PRESSURE = 0xD0,  // 1101 0000
	MIDI_PITCH_BEND = 0xE0,        // 1110 0000

	// System Common Messages
	MIDI_SYSTEM_EXCLUSIVE = 0xF0, // 1111 0000
	MIDI_TIME_CODE = 0xF1,        // 1111 0001
	MIDI_SONG_POSITION = 0xF2,    // 1111 0010
	MIDI_SONG_SELECT = 0xF3,      // 1111 0011
	MIDI_TUNE_REQUEST = 0xF6,     // 1111 0110
	MIDI_END_OF_EXCLUSIVE = 0xF7, // 1111 0111

	// System Real-Time Messages
	MIDI_TIMING_CLOCK = 0xF8,   // 1111 1000
	MIDI_START = 0xFA,          // 1111 1010
	MIDI_CONTINUE = 0xFB,       // 1111 1011
	MIDI_STOP = 0xFC,           // 1111 1100
	MIDI_ACTIVE_SENSING = 0xFE, // 1111 1110
	MIDI_SYSTEM_RESET = 0xFF,   // 1111 1111

	// Section Starts
	MIDI_SYSTEM_COMMON_MSG = MIDI_SYSTEM_EXCLUSIVE,
	MIDI_SYSTEM_REAL_TIME_MSG = MIDI_TIMING_CLOCK,

} MidiMessageType;

// clang-format off
static const s8 midi_cc_table[128] = {
	//					0			1			2			3			4			5			6			7
	/*   0 */			-1,			-1,			P_NOISE,	P_SENS,		P_DRIVE,	P_GLIDE,	-1,			P_MIXSYNTH,
	/*   8 */			P_MIXWETDRY,P_PITCH,	-1,			P_GATE_LENGTH,P_DLTIME,	P_PWM,		P_INTERVAL,	P_SMP_POS,
	/*  16 */			P_SMP_GRAINSIZE,P_SMP_RATE,P_SMP_TIME,P_ENV_LEVEL,P_A2,		P_D2,		P_S2,		P_R2,
	/*  24 */			P_AFREQ,	P_ADEPTH,	P_AOFFSET,	P_BFREQ,	P_BDEPTH,	P_BOFFSET,	-1,			P_MIXHPF,

	// CCs 32 through 63 reserved for 14-bit CCs, each of them representing the LSB of CC [number - 32]
	/*  32 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  40 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  48 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  56 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
	/*  64 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			P_MIXRESO, // reso needs to be moved
	// End of 14-bit CC reserved numbers

	/*  72 */			P_R,		P_A,		P_S,		P_D,		P_XFREQ,	P_XDEPTH,	P_XOFFSET,	P_YFREQ,
	/*  80 */			P_YDEPTH,	P_YOFFSET,	P_SAMPLE,	P_SEQPAT,	-1,			P_SEQSTEP,	-1,			-1,
	/*  88 */			-1,			P_MIXINPUT,	P_MIXINWETDRY,P_RVSEND,	P_RVTIME,	P_RVSHIM,	P_DLSEND,	P_DLFB,
	/*  96 */			-1,			-1,			-1,			-1,			-1,			P_LATCHONOFF,			P_ARPONOFF,	P_ARPMODE,
	/* 104 */			P_ARPDIV,	P_ARPPROB,	P_ARPLEN,	P_ARPOCT,	P_SEQMODE,	P_SEQDIV,	P_SEQPROB,	P_SEQLEN,
	/* 112 */			P_DLRATIO,	P_DLWOB,	P_RVWOB,	-1,			P_JIT_POS,	P_JIT_GRAINSIZE, P_JIT_RATE, P_JIT_PULSE,
	/* 120 */			-1,			-1,			-1,			-1,			-1,			-1,			-1,			-1,
};
// clang-format on