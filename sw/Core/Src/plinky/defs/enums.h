#pragma once

#include "gfx/data/icons.h"
#include "synth/params.h"
// clang-format off
#include "wtenum.h"

enum ECats {
	CAT_BLANK,
	CAT_BASS,CAT_LEADS,CAT_PADS,CAT_ARPS,CAT_PLINKS,CAT_PLONKS,CAT_BEEPS,CAT_BOOPS,CAT_SFX,CAT_LINEIN,
	CAT_SAMPLER,CAT_DONK,CAT_JOLLY,CAT_SADNESS,CAT_WILD,CAT_GNARLY,CAT_WEIRD,
	CAT_LAST
};
const char* const kpresetcats[CAT_LAST] = {
"",
"Bass",
"Leads",
"Pads",
"Arps",
"Plinks",
"Plonks",
"Beeps",
"Boops",
"SFX",
"Line-In",
"Sampler",
"Donk",
"Jolly",
"Sadness",
"Wild",
"Gnarly",
"Weird",
};


enum ECVQuant { CVQ_OFF, CVQ_ON, CVQ_SCALE, CVQ_LAST };

enum EArpModes {
	ARP_UP,
	ARP_DOWN,
	ARP_UPDOWN,
	ARP_UPDOWNREP,
	ARP_PEDALUP,
	ARP_PEDALDOWN,
	ARP_PEDALUPDOWN,
	ARP_RANDOM,
	ARP_RANDOM2,
	ARP_ALL,
	ARP_UP8,
	ARP_DOWN8,
	ARP_UPDOWN8,
	ARP_RANDOM8,
	ARP_RANDOM28,
	ARP_LAST,
};

static inline bool isarpmode8step(int mode) {
	return mode >= ARP_UP8;
}

enum ESeqModes {
	SEQ_PAUSE,
	SEQ_FWD,
	SEQ_BACK,
	SEQ_PINGPONG,
	SEQ_PINGPONGREP,
	SEQ_RANDOM,
	SEQ_LAST,
};

enum Scales {
S_MAJOR,			
S_MINOR,			
S_HARMMINOR,		
S_PENTA,			
S_PENTAMINOR,		
S_HIRAJOSHI,		
S_INSEN,			
S_IWATO,			
S_MINYO,			

S_FIFTHS,			
S_TRIADMAJOR,		
S_TRIADMINOR,		

S_DORIAN,			
S_PHYRGIAN,		
S_LYDIAN,			
S_MIXOLYDIAN,		
S_AEOLIAN,			
S_LOCRIAN,			

S_BLUESMINOR,		
S_BLUESMAJOR,		

S_ROMANIAN,		
S_WHOLETONE,		

S_HARMONICS,
S_HEXANY, 
S_JUST, 


S_CHROMATIC,
S_DIMINISHED,
S_LAST,
};



enum ELFOShape {
	LFO_TRI, LFO_SIN, LFO_SMOOTHNOISE, LFO_STEPNOISE, LFO_BISQUARE, LFO_SQUARE, LFO_SANDCASTLE, LFO_SAW, LFO_BITRIGS, LFO_TRIGS, LFO_ENV, LFO_LAST,
};



const char* const scalenames[S_LAST] = {
[S_MAJOR]="Major",
[S_MINOR]="Minor",
[S_HARMMINOR]="Harmonic Min",
[S_PENTA]="Penta Maj",
[S_PENTAMINOR]="Penta Min",
[S_HIRAJOSHI]="Hirajoshi",
[S_INSEN]="Insen",
[S_IWATO]="Iwato",
[S_MINYO]="Minyo",
[S_FIFTHS]="Fifths",
[S_TRIADMAJOR]="Triad Maj",
[S_TRIADMINOR]="Triad Min",
[S_DORIAN]="Dorian",
[S_PHYRGIAN]="Phrygian",
[S_LYDIAN]="Lydian",
[S_MIXOLYDIAN]="Mixolydian",
[S_AEOLIAN]="Aeolian",
[S_LOCRIAN]="Lacrian",
[S_BLUESMINOR]="Blues Min",
[S_BLUESMAJOR]="Blues Maj",
[S_ROMANIAN]="Romanian",
[S_WHOLETONE]="Wholetone",
[S_HARMONICS]="Harmonics",
[S_HEXANY]="Hexany",
[S_JUST]="Just",
[S_CHROMATIC]="Chromatic",
[S_DIMINISHED]="Diminished",
};
const char* const lfonames[LFO_LAST] = {
		[LFO_TRI] = "Triangle",[LFO_SIN] = "Sine",[LFO_SMOOTHNOISE] = "SmthRnd",
		[LFO_STEPNOISE] = "StepRnd",[LFO_BISQUARE] = "BiSquare",
		[LFO_SQUARE] = "Square",[LFO_SANDCASTLE] = "Castle",
		[LFO_BITRIGS] = "BiTrigs",[LFO_TRIGS] = "Trigs",
		[LFO_ENV] = "Env", [LFO_SAW] = "Saw"
};

const char* const cvquantnames[CVQ_LAST] = {
	[CVQ_OFF] = "Off",
	[CVQ_ON] = "On",
	[CVQ_SCALE] = "Scale",
};

const char* const seqmodenames[SEQ_LAST] = {
	[SEQ_PAUSE]="Pause",
	[SEQ_FWD] = "Forward",
	[SEQ_BACK] = "Reverse",
	[SEQ_PINGPONG] = "Pingpong",
	[SEQ_PINGPONGREP] = "PingPong Rep",
	[SEQ_RANDOM] = "Random",
};

const char* const arpmodenames[ARP_LAST] = {
		[ARP_UP] = "Up",
		[ARP_DOWN] = "Down",
		[ARP_UPDOWN] = "Up/Down",
		[ARP_UPDOWNREP] = "Up/Down\nRepeat",
		[ARP_PEDALUP] = "Pedal    \nUp",
		[ARP_PEDALDOWN] = "Pedal    \nDown",
		[ARP_PEDALUPDOWN] = "Pedal\nUp/Down",
		[ARP_RANDOM] = "Rnd",
		[ARP_RANDOM2] = "2xRnd",
		[ARP_ALL] = "Chord",
		[ARP_UP8] = "Up\n8 Steps",
		[ARP_DOWN8] = "Down\n8 Steps",
		[ARP_UPDOWN8] = "Up/Down\n8 Steps",
		[ARP_RANDOM8] = "Rnd\n8 Steps",
		[ARP_RANDOM28] = "2xRnd\n8 Steps",
};



u16 const divisions[DIVISIONS_MAX] = { 1,2, 3,4,5, 6,8,10, 12,16,20, 24,32,40, 48,64,80, 96,128,160, 192, 256 };
//static const char* const divisornames[6] = { "32nd","16th","8th", "quarter", "half","whole" };

