#include "data/tables.h"
#include "hardware/cv.h"
#include "hardware/flash.h"
#include "synth/lfos.h"
#include "synth/params.h"
#include "synth/pattern.h"
#include "synth/pitch_tools.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "ui/ui.h"

// clang-format off
#define QUARTER (PARAM_SIZE/4)
#define EIGHTH (PARAM_SIZE/8)
#define QUANT(v,maxi) ( ((v)*PARAM_SIZE+PARAM_SIZE/2)/(maxi) )

enum {
	SYS_DEPRACATED_ARPON=1,
	SYS_DEPRACATED_LATCHON=2,
};

Preset rampreset;
PatternQuarter rampattern[NUM_QUARTERS];
SysParams sys_params;
u8 ramsample1_idx=255;
u8 rampreset_idx=255;
u8 rampattern_idx=255;
u8 pending_preset = 255;
u8 pending_pattern = 255;
u8 pending_sample1 = 255;

u8 prev_pending_preset = 255;
u8 prev_pending_pattern = 255;
u8 prev_pending_sample1 = 255;

u8 cur_pattern; // this is the current pattern, derived from param, can modulate.
u8 copy_request = 255;
u8 preset_copy_source = 0;
u8 pattern_copy_source = 0;
u8 sample_copy_source = 0;
s8 selected_preset_global; // the thing that gets cleared when you hold down X

float knobbase[2];

u32 flashtime[GEN_LAST]; // for each thing we care about, what have we written to?
u32 ramtime[GEN_LAST]; //...and what has the UI set up? 

SysParams sys_params;
Preset const init_params;

const static bool IsGenDirty(int gen) {
	return ramtime[gen] != flashtime[gen];
}
void SwapParams(int a, int b) {
	for (int k = 0; k < 8; ++k) {
		int t = rampreset.params[a][k];
		rampreset.params[a][k] = rampreset.params[b][k];
		rampreset.params[b][k] = t;
	}
}
bool CopyPresetToRam(bool force) {
	if (rampreset_idx == sys_params.curpreset && !force)
		return true; // nothing to do
	if (flash_busy || IsGenDirty(GEN_PRESET)) return false; // not copied yet
	memcpy(&rampreset, preset_flash_ptr(sys_params.curpreset), sizeof(rampreset));
	for (int m = 1; m < NUM_MOD_SOURCES; ++m)
		rampreset.params[P_VOLUME][m] = 0;
	// upgrade rampreset.version to CUR_PRESET_VERSION
	if (rampreset.version == 0) {
		rampreset.version = 1;
		// swappin these around ;)
		SwapParams(P_MIX_WIDTH,P_ACCEL_SENS);
		rampreset.params[P_MIX_WIDTH][0] = HALF_PARAM_SIZE; // set default
	}
	if (rampreset.version == 1) {
		rampreset.version = 2;
		// insert a new lfo shape at LFO_SAW
		for (int p = 0; p < 4; ++p) {
			s16* data = rampreset.params[P_A_SHAPE + p * 6];
			*data = (*data * (NUM_LFO_SHAPES - 1)) / (NUM_LFO_SHAPES); // rescale to add extra enum entry
			if (*data >= (LFO_SAW * PARAM_SIZE) / NUM_LFO_SHAPES) // and shift high numbers up
				*data += (1 * PARAM_SIZE) / NUM_LFO_SHAPES;
		}
	}
	rampreset_idx = sys_params.curpreset;
	return true;
}
bool CopySampleToRam(bool force) {
	if (ramsample1_idx == cur_sample_id1 && !force)
		return true; // nothing to do
	if (flash_busy || IsGenDirty(GEN_SAMPLE)) return false; // not copied yet
	if (cur_sample_id1 == 0)
		memset(get_sample_info(), 0, sizeof(SampleInfo));
	else
		memcpy(get_sample_info(), sample_info_flash_ptr(cur_sample_id1 - 1), sizeof(SampleInfo));
	ramsample1_idx = cur_sample_id1;
	return true;
}
bool CopyPatternToRam(bool force) {
	if (rampattern_idx == cur_pattern && !force)
		return true; // nothing to do
	if (flash_busy || IsGenDirty(GEN_PAT0) || IsGenDirty(GEN_PAT1) || IsGenDirty(GEN_PAT2) || IsGenDirty(GEN_PAT3)) 
		return false; // not copied yet
	for (int i = 0; i < 4; ++i)
		memcpy(&rampattern[i], ptr_quarter_flash_ptr((cur_pattern) * 4 + i), sizeof(rampattern[0]));
	rampattern_idx = cur_pattern;
	return true;
}


void InitParamsOnBoot(void) {
	// clear remaining state
	pending_preset = -1;
	pending_pattern = -1;
	pending_sample1 = -1;
	rampattern_idx = -1;
	ramsample1_idx = -1;
	rampreset_idx = -1;
	// relocate the first preset and pattern into ram
	copy_request = 255;
	for (int i = 0; i < GEN_LAST; ++i) {
		ramtime[i] = 0;
		flashtime[i] = 0;
	}
	codec_setheadphonevol(sys_params.headphonevol + 45);
	selected_preset_global = sys_params.curpreset;
}

int getheadphonevol(void) { // for emu really
	return sys_params.headphonevol + 45;
}

void SetPreset(u8 preset, bool force) {
	if (preset >= 32)
		return;
	if (preset == sys_params.curpreset && !force)
		return;
	sys_params.curpreset = preset;
	clear_latch();
	CopyPresetToRam(force);
	ramtime[GEN_SYS]=millis();
}
/*
void SetPattern(u8 pattern, bool force) {
	if (pattern >= 24)
		return;
	if (pattern == cur_pattern && !force)
		return;
	cur_pattern = pattern;
	CopyPatternToRam(force);
	ramtime[GEN_SYS]=millis();
}*/

bool NeedWrite(int gen, u32 now) {
	if (ramtime[gen] == flashtime[gen])
		return false;
	if (gen == GEN_PRESET && sys_params.curpreset != rampreset_idx) {
		// the current preset is not equal to the ram preset, but the ram preset is dirty! WE GOTTA WRITE IT NOW!
		return true;
	}
	if (gen == GEN_SAMPLE && cur_sample_id1 != ramsample1_idx) {
		// the current sample is not equal to the ram preset, but the ram sample is dirty! WE GOTTA WRITE IT NOW!
		return true;
	}
	if (gen >= GEN_PAT0 && gen <= GEN_PAT3 && cur_pattern != rampattern_idx) {
		// the current pattern is not equal to the ram pattern, but the ram pattern is dirty! WE GOTTA WRITE IT NOW!
		return true;
	}
	
	u32 age = ramtime[gen] - flashtime[gen];
	if (age > 60000)
		return true;
	u32 time_since_edit = now - ramtime[gen];
	return time_since_edit > 5000;
}

void WritePattern(u32 now) {
#ifndef DISABLE_AUTOSAVE
	for (int i = 0; i < 4; ++i) if (NeedWrite(GEN_PAT0 + i, now)) {
		flashtime[GEN_SYS] = ramtime[GEN_SYS];
		flashtime[GEN_PAT0 + i] = ramtime[GEN_PAT0 + i];
		flash_write_page(&rampattern[i], sizeof(PatternQuarter), PATTERNS_START + (rampattern_idx) * 4 + i);
	}
#endif
}

void WriteSample(u32 now) {
	if (NeedWrite(GEN_SAMPLE, now)) {
		flashtime[GEN_SYS] = ramtime[GEN_SYS];
		flashtime[GEN_SAMPLE] = ramtime[GEN_SAMPLE];
		if (ramsample1_idx > 0)
			flash_write_page(get_sample_info(), sizeof(SampleInfo), F_SAMPLES_START + ramsample1_idx - 1);
	}
}

void WritePreset(u32 now) {
#ifndef DISABLE_AUTOSAVE	
	if (NeedWrite(GEN_PRESET, now) || NeedWrite(GEN_SYS, now)) {
		flashtime[GEN_SYS] = ramtime[GEN_SYS];
		flashtime[GEN_PRESET] = ramtime[GEN_PRESET];
		flash_write_page(&rampreset, sizeof(Preset), rampreset_idx);
	}
#endif
}



void PumpFlashWrites(void) {
	if (sampler_mode != SM_PREVIEW)
		return;
	u32 now = millis();

	if (copy_request != 255) {
		// we want to copy TO copy_request, FROM preset_copy_source
		if (copy_request & 128) {
			// wipe!
			copy_request &= 63;
			if (copy_request < 32) {
				memcpy(&rampreset, &init_params, sizeof(rampreset));
				ramtime[GEN_PRESET] = now;
			}
			else if (copy_request < 64 - 8) {
				memset(&rampattern, 0, sizeof(rampattern));
				ramtime[GEN_PAT0] = now;
				ramtime[GEN_PAT1] = now;
				ramtime[GEN_PAT2] = now;
				ramtime[GEN_PAT3] = now;
			}
			else {
				memset(get_sample_info(), 0, sizeof(SampleInfo));
				ramtime[GEN_SAMPLE] = now;
			}
		}
		else if (copy_request < 64) {
			// copy!
			if (copy_request < 32) {

#ifndef DISABLE_AUTOSAVE				
				if (copy_request == preset_copy_source) {
					// toggle
					WritePreset(now + 100000); // flush any writes
					flash_toggle_preset(preset_copy_source);
					memcpy(&rampreset, preset_flash_ptr(sys_params.curpreset), sizeof(rampreset));

				}
				else {
					// copy preset
					flash_write_page(preset_flash_ptr(preset_copy_source), sizeof(Preset), copy_request);
				}
#endif

				SetPreset(copy_request, true);
			} 
			else if (copy_request < 64 - 8) {
				int srcpat = pattern_copy_source;
				int dstpat = copy_request - 32;
				{
#ifndef DISABLE_AUTOSAVE					
					// copy pattern
					for (int i = 0; i < 4; ++i)
						flash_write_page(ptr_quarter_flash_ptr(srcpat * 4 + i), sizeof(PatternQuarter), 32 + dstpat * 4 + i);
#endif
				}
				save_param(P_PATTERN, SRC_BASE, dstpat);
			}
		}
		copy_request = 255;
		ui_mode = UI_DEFAULT;
	}
	

	WritePattern(now);
	WriteSample(now);
	WritePreset(now);
	
}

Preset const init_params = {
	.seq_len=8,
	.version=CUR_PRESET_VERSION,
	.params=
	{
		[P_ENV_LVL1] = {HALF_PARAM_SIZE},
		[P_DISTORTION] = {0},
		[P_ATTACK1] = {EIGHTH},
		[P_DECAY1] = {QUARTER},
		[P_SUSTAIN1] = {PARAM_SIZE},
		[P_RELEASE1] = {EIGHTH},

		[P_OCT] = {0,0,0},
		[P_PITCH] = {0,0,0},
		[P_SCALE] = {QUANT(S_MAJOR,NUM_SCALES)},
		[P_MICROTONE] = {EIGHTH},
		[P_COLUMN] = {QUANT(7,13)},
		[P_INTERVAL] = {(0 * PARAM_SIZE) / 12},
		[P_DEGREE] = {0,0,0},

		[P_NOISE] = {0,0,0},

		[P_SMP_SPEED] = {HALF_PARAM_SIZE},
		[P_SMP_GRAINSIZE] = {HALF_PARAM_SIZE},
		[P_SMP_STRETCH] = {HALF_PARAM_SIZE},
		

		//		[P_ARP_ORDER]={QUANT(ARP_UP,NUM_ARP_ORDERS)},
				[P_ARP_CLK_DIV] = {QUANT(2,NUM_SYNC_DIVS) },
				[P_ARP_CHANCE] = {PARAM_SIZE},
				[P_ARP_EUC_LEN] = {QUANT(8,17)},
				[P_ARP_OCTAVES] = {QUANT(0,4)},
				[P_GLIDE] = {0},

				[P_SEQ_ORDER] = {QUANT(SEQ_ORD_FWD,NUM_SEQ_ORDERS)},
				[P_SEQ_CLK_DIV] = {QUANT(6,NUM_SYNC_DIVS+1)},
				[P_SEQ_CHANCE] = {PARAM_SIZE},
				[P_SEQ_EUC_LEN] = {QUANT(8,17)},
				[P_PATTERN] = {QUANT(0,24)},
				[P_STEP_OFFSET] = {0},
				[P_TEMPO] = {0},

				[P_GATE_LENGTH] = {PARAM_SIZE},

				//[P_DLY_SEND]={HALF_PARAM_SIZE},
				[P_DLY_TIME] = {QUANT(3,8)},
				[P_DLY_FEEDBACK] = {HALF_PARAM_SIZE},
				//[P_DLCOLOR]={PARAM_SIZE},
				[P_DLY_WOBBLE] = {QUARTER},
				[P_DLY_PINGPONG] = {PARAM_SIZE},

				[P_RVB_SEND]={QUARTER},
				[P_RVB_TIME] = {HALF_PARAM_SIZE},
				[P_RVB_SHIMMER] = {QUARTER},
				//[P_RVCOLOR]={PARAM_SIZE-QUARTER},
				[P_RVB_WOBBLE] = {QUARTER},
				//[P_RVB_UNUSED]={0},


				[P_SYNTH_LVL] = {HALF_PARAM_SIZE},
				[P_MIX_WIDTH] = {(HALF_PARAM_SIZE * 7)/8},
				[P_INPUT_WET_DRY] = {0},
				[P_INPUT_LVL] = {HALF_PARAM_SIZE},
				[P_SYNTH_WET_DRY] = {0},
							
		[P_ATTACK2] = {EIGHTH},
		[P_DECAY2] = {QUARTER},
		[P_SUSTAIN2] = {PARAM_SIZE},
		[P_RELEASE2] = {EIGHTH},
		[P_SWING] = {0},
				[P_ENV_LVL2] = {HALF_PARAM_SIZE},
				[P_CV_QUANT] = {QUANT(CVQ_OFF,CVQ_LAST)},

				[P_A_OFFSET] = {0},
				[P_A_SCALE] = {HALF_PARAM_SIZE},
				[P_A_DEPTH] = {0},
				[P_A_RATE] = {0},
				//[P_A_SHAPE] = {QUANT(LFO_ENV,NUM_LFO_SHAPES)},
				[P_A_SYM] = {0},

				[P_B_OFFSET] = {0},
				[P_B_SCALE] = {HALF_PARAM_SIZE},
				[P_B_DEPTH] = {0},
				[P_B_RATE] = {100},
				[P_B_SHAPE] = {0},
				[P_B_SYM] = {0},

				[P_X_OFFSET] = {0},
				[P_X_SCALE] = {HALF_PARAM_SIZE},
				[P_X_DEPTH] = {0},
				[P_X_RATE] = {-123},
				[P_X_SHAPE] = {0},
				[P_X_SYM] = {0},

				[P_Y_OFFSET] = {0},
				[P_Y_SCALE] = {HALF_PARAM_SIZE},
				[P_Y_DEPTH] = {0},
				[P_Y_RATE] = {-315},
				[P_Y_SHAPE] = {0},
				[P_Y_SYM] = {0},

				[P_ACCEL_SENS] = {HALF_PARAM_SIZE},

				[P_MIDI_CH_IN] = {0},
				[P_MIDI_CH_OUT] = {0},
				}
}; // init params