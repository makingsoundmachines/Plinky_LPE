#pragma once

#include "gfx/data/icons.h"
#include "hardware/cv.h"
#include "synth/params.h"
// clang-format off

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

enum ELFOShape {
	LFO_TRI, LFO_SIN, LFO_SMOOTHNOISE, LFO_STEPNOISE, LFO_BISQUARE, LFO_SQUARE, LFO_SANDCASTLE, LFO_SAW, LFO_BITRIGS, LFO_TRIGS, LFO_ENV, LFO_LAST,
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

//static const char* const divisornames[6] = { "32nd","16th","8th", "quarter", "half","whole" };

