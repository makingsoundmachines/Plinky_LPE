#include "data/tables.h"
#include "synth/params.h"
#include "synth/pitch_tools.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"

// clang-format off
static inline u8 lfohashi(u16 step) {
	return rndtab[step];
}
static inline float lfohashf(u16 step) {
	return (float) (lfohashi(step) * (2.f / 256.f) - 1.f);
}

float EvalTri(float t, u32 step) {
	return 1.f - (t + t);
}
float EvalEnv(float t, u32 step) {
	// unipolar pseudo exponential up/down
	if (step & 1) {
		t *= t;
		t *= t;
		return t;
	}
	else {
		t = 1.f - t;
		t *= t;
		t *= t;
		return 1.f-t;
	}
}
float EvalSin(float t, u32 step) {
	t = t * t * (3.f - t - t);
	return 1.f - (t + t);
}
float EvalSaw(float t, u32 step) {
	return (step &1) ? t-1.f : 1.f-t;
}
float EvalSquare(float t, u32 step) {
	return (step & 1) ? 0.f : 1.f;
}
float EvalBiSquare(float t, u32 step) {
	return (step & 1) ? -1.f : 1.f;
}
float EvalSandcastle(float t, u32 step) {
	return (step & 1) ? ((t<0.5f) ? 0.f : -1.f) : ((t<0.5f) ? 1.f : 0.f);
}
static inline float triggy(float t) {
	t=1.f-(t+t);
	t=t*t;
	return t*t;
}
float EvalTrigs(float t, u32 step) {
	return (step & 1) ? ((t<0.5f) ? 0.f : triggy(1.f-t)) : ((t<0.5f) ? triggy(t) : 0.f);
}
float EvalBiTrigs(float t, u32 step) {
	return (step & 1) ? ((t<0.5f) ? 0.f : -triggy(1.f-t)) : ((t<0.5f) ? triggy(t) : 0.f);
}
float EvalStepNoise(float t, u32 step) {
	return lfohashf(step);
}
float EvalSmoothNoise(float t, u32 step) {
	float n0 = lfohashf(step + (step&1)), n1 = lfohashf(step | 1);
	return n0 + (n1 - n0) * t;
}

float (*lfofuncs[LFO_LAST])(float t, u32 step) = {
		[LFO_TRI]=EvalTri, [LFO_SIN]=EvalSin, [LFO_SMOOTHNOISE]=EvalSmoothNoise, [LFO_STEPNOISE]=EvalStepNoise,
		[LFO_BISQUARE]=EvalBiSquare, [LFO_SQUARE]=EvalSquare, [LFO_SANDCASTLE]=EvalSandcastle, [LFO_BITRIGS]=EvalBiTrigs, [LFO_TRIGS]=EvalTrigs, [LFO_ENV]=EvalEnv,
		[LFO_SAW]=EvalSaw,
};

float lfo_eval(u32 ti, float warp, unsigned int shape) {
	int step = (ti >> 16)<<1;
	float t = (ti & 65535) * (1.f / 65536.f);
	if (t < warp)
		t /= warp;
	else {
		step++;
		t = (1.f - t) / (1.f-warp);
	}
	if (shape>=LFO_LAST) shape=0;
	return (*lfofuncs[shape])(t, step);
}

int params_premod[P_LAST]; // parameters with the lfos/inputs pre-mixed in
#define FULLBITS 10
#define QUARTER (FULL/4)
#define EIGHTH (FULL/8)
#define QUANT(v,maxi) ( ((v)*FULL+FULL/2)/(maxi) )

#define FIRST_PRESET_IDX 0
#define LAST_PRESET_IDX 32
#define FIRST_PATTERN_IDX LAST_PRESET_IDX
#define LAST_PATTERN_IDX 128 // 24 patterns x 4 quarters = 96 pages starting from page 32
#define FIRST_SAMPLE_IDX LAST_PATTERN_IDX
#define LAST_SAMPLE_IDX 136
#define LAST_IDX LAST_SAMPLE_IDX
#define OPS_IDX 0xfe
typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;
enum {
	SYS_DEPRACATED_ARPON=1,
	SYS_DEPRACATED_LATCHON=2,
};
// preset version 1: ??
// preset version 2: add SAW lfo shape
#define CUR_PRESET_VERSION 2
Preset rampreset;
PatternQuarter rampattern[NUM_QUARTERS];
SysParams sysparams;
u8 ramsample1_idx=255;
u8 rampreset_idx=255;
u8 rampattern_idx=255;
u8 updating_bank2 = 0;
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
u8 recording_knobs = 0;
s8 selected_preset_global; // the thing that gets cleared when you hold down X

float knobbase[2];

u32 flashtime[GEN_LAST]; // for each thing we care about, what have we written to?
u32 ramtime[GEN_LAST]; //...and what has the UI set up? 

typedef struct FlashPage {
	union {
		u8 raw[2048 - sizeof(SysParams) - sizeof(PageFooter)];
		Preset preset;
		PatternQuarter patternquarter;
		SampleInfo sampleinfo;
	};
	SysParams sysparams;
	PageFooter footer;
} FlashPage;
static_assert(sizeof(FlashPage) == 2048, "?");
u8 latestpagesidx[LAST_IDX];
u8 backuppagesidx[LAST_PRESET_IDX];
SysParams sysparams;
static inline FlashPage* GetFlashPagePtr(u8 page) { return (FlashPage*)(FLASH_ADDR_256 + page * 2048); }

Preset const init_params;
int mod_cur[8]; // 16 bit fp


static inline int GetHeadphoneAsParam(void) {
	return (sysparams.headphonevol + 45) * (FULL / 64);
}

static inline int param_eval_premod(u8 paramidx) {
	if (paramidx == P_HEADPHONE)
		return params_premod[paramidx] = GetHeadphoneAsParam() * 65536 ;
	s16* p = rampreset.params[paramidx];
	int tv = p[M_BASE] << 16;
	tv += (mod_cur[M_A] * p[M_A]);
	tv += (mod_cur[M_B] * p[M_B]);
	tv += (mod_cur[M_X] * p[M_X]);
	tv += (mod_cur[M_Y] * p[M_Y]);
	params_premod[paramidx] = tv;
	return tv;
}



static inline Preset* GetSavedPreset(u8 presetidx) {
#ifdef HALF_FLASH
	return (Preset*)&init_params;
#endif
	if (presetidx >= 32)
		return (Preset*)&init_params;
	FlashPage*fp=GetFlashPagePtr(latestpagesidx[presetidx]);
	if (fp->footer.idx!=presetidx || fp->footer.version!=2)
		return (Preset*)&init_params;
	return (Preset * )fp;
}
static inline PatternQuarter* GetSavedPatternQuarter(u8 patternq) {
#ifdef HALF_FLASH
	return (PatternQuarter*)zero;
#endif
	if (patternq >= 24*4)
		return (PatternQuarter*)zero;
	FlashPage* fp = GetFlashPagePtr(latestpagesidx[patternq + FIRST_PATTERN_IDX]);
	if (fp->footer.idx != patternq+ FIRST_PATTERN_IDX || fp->footer.version != 2)
		return (PatternQuarter*)zero;
	return (PatternQuarter*)fp;
}
SampleInfo* GetSavedSampleInfo(u8 sample0) {
#ifdef HALF_FLASH
	return (SampleInfo*)zero;
#endif
	if (sample0 >= 8)
		return (SampleInfo*)zero;
	FlashPage* fp = GetFlashPagePtr(latestpagesidx[sample0 + FIRST_SAMPLE_IDX]);
	if (fp->footer.idx != sample0 + FIRST_SAMPLE_IDX || fp->footer.version != 2)
		return (SampleInfo*)zero;
	return (SampleInfo*)fp;
}

u16 computehash(const void* data, int nbytes) {
	u16 hash = 123;
	const u8* src = (const u8 * )data;
	for (int i=0;i<nbytes;++i)
		hash = hash* 23 + *src++;
	return hash;
}

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
	if (rampreset_idx == sysparams.curpreset && !force)
		return true; // nothing to do
	if (updating_bank2 || IsGenDirty(GEN_PRESET)) return false; // not copied yet
	memcpy(&rampreset, GetSavedPreset(sysparams.curpreset), sizeof(rampreset));
	for (int m = 1; m < M_LAST; ++m)
		rampreset.params[P_HEADPHONE][m] = 0;
	// upgrade rampreset.version to CUR_PRESET_VERSION
	if (rampreset.version == 0) {
		rampreset.version = 1;
		// swappin these around ;)
		SwapParams(P_MIX_WIDTH,P_ACCEL_SENS);
		rampreset.params[P_MIX_WIDTH][0] = HALF; // set default
	}
	if (rampreset.version == 1) {
		rampreset.version = 2;
		// insert a new lfo shape at LFO_SAW
		for (int p = 0; p < 4; ++p) {
			s16* data = rampreset.params[P_ASHAPE + p * 6];
			*data = (*data * (LFO_LAST - 1)) / (LFO_LAST); // rescale to add extra enum entry
			if (*data >= (LFO_SAW * FULL) / LFO_LAST) // and shift high numbers up
				*data += (1 * FULL) / LFO_LAST;
		}
	}
	rampreset_idx = sysparams.curpreset;
	return true;
}
bool CopySampleToRam(bool force) {
	if (ramsample1_idx == cur_sample_id1 && !force)
		return true; // nothing to do
	if (updating_bank2 || IsGenDirty(GEN_SAMPLE)) return false; // not copied yet
	if (cur_sample_id1 == 0)
		memset(get_sample_info(), 0, sizeof(SampleInfo));
	else
		memcpy(get_sample_info(), GetSavedSampleInfo(cur_sample_id1 - 1), sizeof(SampleInfo));
	ramsample1_idx = cur_sample_id1;
	return true;
}
bool CopyPatternToRam(bool force) {
	if (rampattern_idx == cur_pattern && !force)
		return true; // nothing to do
	if (updating_bank2 || IsGenDirty(GEN_PAT0) || IsGenDirty(GEN_PAT1) || IsGenDirty(GEN_PAT2) || IsGenDirty(GEN_PAT3)) 
		return false; // not copied yet
	for (int i = 0; i < 4; ++i)
		memcpy(&rampattern[i], GetSavedPatternQuarter((cur_pattern) * 4 + i), sizeof(rampattern[0]));
	rampattern_idx = cur_pattern;
	return true;
}


u8 next_free_page=0;
static u32 next_seq = 0;
void InitParamsOnBoot(void) {
	u8 dummypage = 0;
	memset(latestpagesidx, dummypage, sizeof(latestpagesidx));
	memset(backuppagesidx, dummypage, sizeof(backuppagesidx));
	u32 highest_seq = 0;
	next_free_page = 0;
	memset(&sysparams, 0, sizeof(sysparams));
#ifndef HALF_FLASH
	// scan for the latest page for each object
	for (int page = 0 ; page < 255; ++page) {
		FlashPage* p = GetFlashPagePtr(page);
		int i = p->footer.idx;
		if (i >= LAST_IDX)
			continue;// skip blank
		if (p->footer.version < 2)
			continue; // skip old
		u16 check = computehash(p, 2040);
		if (check != p->footer.crc) {
			DebugLog("flash page %d has a bad crc!\r\n", page);
			if (page == dummypage) {
				// shit, the dummy page is dead! move to a different dummy
				for (int i = 0; i < sizeof(latestpagesidx); ++i) if (latestpagesidx[i] == dummypage)
					latestpagesidx[i]++;
				for (int i = 0; i < sizeof(backuppagesidx); ++i) if (backuppagesidx[i] == dummypage)
					backuppagesidx[i]++;
				dummypage++;
			}
			continue;
		}
		if (p->footer.seq > highest_seq) {
			highest_seq = p->footer.seq;
			next_free_page = page + 1;
			sysparams = p->sysparams;
		}
		FlashPage* existing = GetFlashPagePtr(latestpagesidx[i]);
		if (existing->footer.idx!=i  || p->footer.seq > existing->footer.seq || existing->footer.version<2)
			latestpagesidx[i] = page;
	}
#endif
	next_seq = highest_seq + 1;
	memcpy(backuppagesidx, latestpagesidx, sizeof(backuppagesidx));
	
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
	codec_setheadphonevol(sysparams.headphonevol + 45);
	selected_preset_global = sysparams.curpreset;
}

int getheadphonevol(void) { // for emu really
	return sysparams.headphonevol + 45;
}

u8 AllocAndEraseFlashPage(void) {
#ifdef HALF_FLASH
	return 255;
#endif
	while (1) {
		FlashPage* p = GetFlashPagePtr(next_free_page);
		bool inuse = next_free_page == 255;
		inuse |= (p->footer.idx < LAST_IDX&& latestpagesidx[p->footer.idx] == next_free_page);
		inuse |= (p->footer.idx < LAST_PRESET_IDX&& backuppagesidx[p->footer.idx] == next_free_page);
		if (inuse) {
			++next_free_page;
			continue;
		}
		DebugLog("erasing flash page %d\r\n", next_free_page);
		flash_erase_page(next_free_page);
		return next_free_page++;
	}
}


void ProgramPage(void* datasrc, u32 datasize, u8 index) {
#ifndef HALF_FLASH
	updating_bank2 = 1;
	HAL_FLASH_Unlock();
	u8 page = AllocAndEraseFlashPage();
	u8* dst = (u8*)(FLASH_ADDR_256 + page * 2048);
	flash_program_block(dst, datasrc, datasize);
	flash_program_block(dst + 2048 - sizeof(SysParams) - sizeof(PageFooter), &sysparams, sizeof(SysParams));
	PageFooter f;
	f.idx = index;
	f.seq = next_seq++;
	f.version = 2;
	f.crc = computehash(dst, 2040);
	flash_program_block(dst + 2040, &f, 8);
	HAL_FLASH_Lock();
	latestpagesidx[index] = page;
	updating_bank2 = 0;
#endif
}

void SetPreset(u8 preset, bool force) {
	if (preset >= 32)
		return;
	if (preset == sysparams.curpreset && !force)
		return;
	sysparams.curpreset = preset;
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


int GetParam(u8 paramidx, u8 mod) {
	if (paramidx == P_HEADPHONE)
		return mod ? 0 : GetHeadphoneAsParam();
	return rampreset.params[paramidx][mod];
}

void EditParamNoQuant(u8 paramidx, u8 mod, s16 data) {
	if (paramidx >= P_LAST || mod >= M_LAST)
		return;
	if (paramidx == P_HEADPHONE) {
		if (mod == M_BASE) {
			data = clampi(-45, ((data + (FULL/128)) / (FULL / 64)) - 45, 18);
			if (data == sysparams.headphonevol)
				return;
			sysparams.headphonevol = data;
			ramtime[GEN_SYS] = millis();
		}
		return;
	}
	if (!CopyPresetToRam(false))
		return; // oh dear we haven't backed up the previous one yet!
	int olddata = GetParam(paramidx, mod);
	if (olddata == data)
		return;
	rampreset.params[paramidx][mod] = data;
	param_eval_premod(paramidx);
	//if (paramidx == P_SEQSTEP && mod == M_BASE)
	//	return; // dont set dirty when you're just moving the current playhead pos.
	ramtime[GEN_PRESET]=millis();
}

void EditParamQuant(u8 paramidx, u8 mod, s16 data) {
	int max = param_flags[paramidx] & FLAG_MASK;
	if (max>0) {
		data %= max; 
		if (data < 0 && !(param_flags[paramidx] & FLAG_SIGNED))
			data += max;
		data = ((data * 2 + 1) * FULL) / (max * 2);
	}
	EditParamNoQuant(paramidx, mod, data);
}

bool NeedWrite(int gen, u32 now) {
	if (ramtime[gen] == flashtime[gen])
		return false;
	if (gen == GEN_PRESET && sysparams.curpreset != rampreset_idx) {
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
		ProgramPage(&rampattern[i], sizeof(PatternQuarter), FIRST_PATTERN_IDX + (rampattern_idx) * 4 + i);
	}
#endif
}

void WriteSample(u32 now) {
	if (NeedWrite(GEN_SAMPLE, now)) {
		flashtime[GEN_SYS] = ramtime[GEN_SYS];
		flashtime[GEN_SAMPLE] = ramtime[GEN_SAMPLE];
		if (ramsample1_idx > 0)
			ProgramPage(get_sample_info(), sizeof(SampleInfo), FIRST_SAMPLE_IDX + ramsample1_idx - 1);
	}
}

void WritePreset(u32 now) {
#ifndef DISABLE_AUTOSAVE	
	if (NeedWrite(GEN_PRESET, now) || NeedWrite(GEN_SYS, now)) {
		flashtime[GEN_SYS] = ramtime[GEN_SYS];
		flashtime[GEN_PRESET] = ramtime[GEN_PRESET];
		ProgramPage(&rampreset, sizeof(Preset), rampreset_idx);
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
					int t = backuppagesidx[preset_copy_source];
					backuppagesidx[preset_copy_source] = latestpagesidx[preset_copy_source];
					latestpagesidx[preset_copy_source] = t;
					memcpy(&rampreset, GetSavedPreset(sysparams.curpreset), sizeof(rampreset));

				}
				else {
					// copy preset
					ProgramPage(GetSavedPreset(preset_copy_source), sizeof(Preset), copy_request);
				}
#endif

				SetPreset(copy_request, true);
			} 
			else if (copy_request < 64 - 8) {
				int srcpat = pattern_copy_source;
				int dstpat = copy_request - 32;
				/*if (srcpat == dstpat) { toggle not available for patterns
					// toggle
					WritePattern(now + 100000); // flush any writes
					for (int i = 0; i < 4; ++i) {
						int j = srcpat * 4 + i + 32;
						int t = backuppagesidx[j];
						backuppagesidx[j] = latestpagesidx[j];
						latestpagesidx[j] = t;
						memcpy(&rampattern[i], GetSavedPatternQuarter(srcpat*4+i), sizeof(PatternQuarter));
					}
				}
				else */
				{
#ifndef DISABLE_AUTOSAVE					
					// copy pattern
					for (int i = 0; i < 4; ++i)
						ProgramPage(GetSavedPatternQuarter(srcpat * 4 + i), sizeof(PatternQuarter), 32 + dstpat * 4 + i);
#endif
				}
				EditParamQuant(P_SEQPAT, M_BASE, dstpat);
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
		[P_SENS] = {HALF},
		[P_DRIVE] = {0},
		[P_A] = {EIGHTH},
		[P_D] = {QUARTER},
		[P_S] = {FULL},
		[P_R] = {EIGHTH},

		[P_OCT] = {0,0,0},
		[P_PITCH] = {0,0,0},
		[P_SCALE] = {QUANT(S_MAJOR,S_LAST)},
		[P_MICROTUNE] = {EIGHTH},
		[P_STRIDE] = {QUANT(7,13)},
		[P_INTERVAL] = {(0 * FULL) / 12},
		[P_ROTATE] = {0,0,0},

		[P_NOISE] = {0,0,0},

		[P_SMP_RATE] = {HALF},
		[P_SMP_GRAINSIZE] = {HALF},
		[P_SMP_TIME] = {HALF},
		

		//		[P_ARPMODE]={QUANT(ARP_UP,NUM_ARP_ORDERS)},
				[P_ARPDIV] = {QUANT(2,NUM_SYNC_DIVS) },
				[P_ARPPROB] = {FULL},
				[P_ARPLEN] = {QUANT(8,17)},
				[P_ARPOCT] = {QUANT(0,4)},
				[P_GLIDE] = {0},

				[P_SEQMODE] = {QUANT(SEQ_ORD_FWD,NUM_SEQ_ORDERS)},
				[P_SEQDIV] = {QUANT(6,NUM_SYNC_DIVS+1)},
				[P_SEQPROB] = {FULL},
				[P_SEQLEN] = {QUANT(8,17)},
				[P_SEQPAT] = {QUANT(0,24)},
				[P_SEQSTEP] = {0},
				[P_TEMPO] = {0},

				[P_GATE_LENGTH] = {FULL},

				//[P_DLSEND]={HALF},
				[P_DLTIME] = {QUANT(3,8)},
				[P_DLFB] = {HALF},
				//[P_DLCOLOR]={FULL},
				[P_DLWOB] = {QUARTER},
				[P_DLRATIO] = {FULL},

				[P_RVSEND]={QUARTER},
				[P_RVTIME] = {HALF},
				[P_RVSHIM] = {QUARTER},
				//[P_RVCOLOR]={FULL-QUARTER},
				[P_RVWOB] = {QUARTER},
				//[P_RVUNUSED]={0},


				[P_MIXSYNTH] = {HALF},
				[P_MIX_WIDTH] = {(HALF * 7)/8},
				[P_MIXINWETDRY] = {0},
#ifdef EMU
				[P_MIXINPUT] = {0},
#else
				[P_MIXINPUT] = {HALF},
#endif
				[P_MIXWETDRY] = {0},
							
#ifdef NEW_LAYOUT
		[P_A2] = {EIGHTH},
		[P_D2] = {QUARTER},
		[P_S2] = {FULL},
		[P_R2] = {EIGHTH},
		[P_SWING] = {0},
#else
				[P_ENV_RATE] = {QUARTER},
				[P_ENV_REPEAT] = {0},
				[P_ENV_WARP] = {-FULL},
#endif
				[P_ENV_LEVEL] = {HALF},
				[P_CV_QUANT] = {QUANT(CVQ_OFF,CVQ_LAST)},

				[P_AOFFSET] = {0},
				[P_ASCALE] = {HALF},
				[P_ADEPTH] = {0},
				[P_AFREQ] = {0},
				//[P_ASHAPE] = {QUANT(LFO_ENV,LFO_LAST)},
				[P_AWARP] = {0},

				[P_BOFFSET] = {0},
				[P_BSCALE] = {HALF},
				[P_BDEPTH] = {0},
				[P_BFREQ] = {100},
				[P_BSHAPE] = {0},
				[P_BWARP] = {0},

				[P_XOFFSET] = {0},
				[P_XSCALE] = {HALF},
				[P_XDEPTH] = {0},
				[P_XFREQ] = {-123},
				[P_XSHAPE] = {0},
				[P_XWARP] = {0},

				[P_YOFFSET] = {0},
				[P_YSCALE] = {HALF},
				[P_YDEPTH] = {0},
				[P_YFREQ] = {-315},
				[P_YSHAPE] = {0},
				[P_YWARP] = {0},

				[P_ACCEL_SENS] = {HALF},

				[P_MIDI_CH_IN] = {0},
				[P_MIDI_CH_OUT] = {0},
				}
}; // init params


u8 lfo_history[16][4];
u8 lfo_history_pos;
uint64_t lfo_pos[4];
u16 finger_rnd[8] = { 0, 1 << 12, 2 << 12, 3 << 12, 4 << 12, 5 << 12, 6 << 12, 7 << 12 }; // incremented by a big prime each time the finger is triggered
u16 any_rnd = { 8 << 12 }; // incremented every time any finger goes down
int tilt16 = 0; // average tilt, 
int env16 = 0; // global attack/decay env - TODO
int pressure16 = 0; // max pressure
static inline int index_to_tilt16(int fingeridx) {
	return fingeridx * 16384 - (28672*2);
}
static inline int param_eval_int_noscale(u8 paramidx, int rnd, int env16, int pressure16) { // 16 bit fp
	s16* p = rampreset.params[paramidx];
	int tv = params_premod[paramidx]; // p[M_BASE] * 65538;
	if (p[M_RND]) {
		u16 ri = (u16)(rnd + paramidx);
		if (p[M_RND] > 0)
			// unsigned uniform distribution
			tv += (rndtab[ri] * p[M_RND]) << 8;
		else {
			// signed! triangular distribution
			ri += ri;
			tv += (((int)rndtab[ri] - (int)rndtab[ri - 1]) * p[M_RND]) << 8;
		}
	}
	//tv += (tilt16 * p[M_TILT]);
	tv += (env16 * p[M_ENV]);
	tv += (maxi(0,pressure16) * p[M_PRESSURE]);
	/*
	tv += (mod_cur[M_A] * p[M_A]);
	tv += (mod_cur[M_B] * p[M_B]);
	tv += (mod_cur[M_X] * p[M_X]);
	tv += (mod_cur[M_Y] * p[M_Y]);
	*/
	u8 flags = param_flags[paramidx];
	u8 maxi = flags & FLAG_MASK;
	return clampi(tv >> FULLBITS, (flags & FLAG_SIGNED) ? -65536 : 0, maxi ? 65535 : 65536);
}

int param_eval_int(u8 paramidx, int rnd, int env16, int pressure16) { // 16 bit fp
	u8 flags = param_flags[paramidx];
	u8 maxi = flags & FLAG_MASK;
	int tv = param_eval_int_noscale(paramidx, rnd, env16, pressure16);
	if (maxi) {
		tv = (tv * maxi) >> 16;
	}
	return tv;
}

