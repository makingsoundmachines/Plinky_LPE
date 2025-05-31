#include "sampler.h"
#include "audio.h"
#include "data/tables.h"
#include "hardware/adc_dac.h"
#include "params.h"
#include "strings.h"
#include "time.h"
#include "ui/pad_actions.h"
#include "ui/ui.h"

// cleanup
extern volatile u8 spistate;
extern short* delaybuf;
extern s16 audioin_peak;
extern u32 record_flashaddr_base;
extern u8 pending_sample1;

extern u8 ramsample1_idx;

int spi_readgrain_dma(int gi);
void reverb_clear(void);
void delay_clear(void);
SampleInfo* sample_info_flash_ptr(u8 sample0);
bool CopySampleToRam(bool force);
// -- cleanup

#define SMUAD(o, a, b) asm("smuad %0, %1, %2" : "=r"(o) : "r"(a), "r"(b))

SamplerMode sampler_mode = SM_PREVIEW;

int grain_pos[32];
s16 grain_buf[GRAINBUF_BUDGET];
s16 grain_buf_end[32]; // for each of the 32 grain fetches, where does it end in the grain_buf?

static SampleInfo cur_sample_info;
static u8 edit_sample_id0 = 0; // this is the one we are editing. no modulation. sample 0-7. not 1 based!
u8 cur_sample_id1 = 0; // this is the one we are playing, derived from param, can modulate. 0 means off, 1-8 is sample
u8 cur_slice_id = 0;   // active slice id, used during recording and to register/adjust slice points

// used while recording a new sample
u32 buf_start_pos = 0;
u32 buf_write_pos = 0;
u32 buf_read_pos = 0;

// static float smooth_lpg(ValueSmoother* s, s32 out, float drive, float noise, float env1_lvl) {
// 	s16 n = ((s16*)rndtab)[rand() & 16383];
// 	float cutoff = 1.f - squaref(maxf(0.f, 1.f - env1_lvl * 1.1f));
// 	s->y1 += (out * drive + n * noise - s->y1) * cutoff;
// 	return s->y1;
// }

SampleInfo* get_sample_info(void) {
	return &cur_sample_info;
}

int using_sampler(void) {
	return get_sample_info()->samplelen;
}

void open_sampler(u8 with_sample_id) {
	edit_sample_id0 = with_sample_id;
	save_param(P_SAMPLE, SRC_BASE, edit_sample_id0 + 1);
	memcpy(&cur_sample_info, sample_info_flash_ptr(edit_sample_id0), sizeof(SampleInfo));
	ramsample1_idx = cur_sample_id1 = edit_sample_id0 + 1;
	pending_sample1 = 255;
	ui_mode = UI_SAMPLE_EDIT;
	cur_slice_id = 7;
}

// == PLAY SAMPLER AUDIO == //

// start of current (slice) loop
static int calcloopstart(u8 slice_id) {
	int all = cur_sample_info.loop & 2;
	return (all) ? 0 : cur_sample_info.splitpoints[slice_id];
}

// end of current (slice) loop
static int calcloopend(u8 slice_id) {
	int all = cur_sample_info.loop & 2;
	return (all || slice_id >= 7) ? cur_sample_info.samplelen - 192 : cur_sample_info.splitpoints[slice_id + 1];
}

void handle_sampler_audio(u32* dst, u32* audioin) {
	cur_sample_id1 = edit_sample_id0 + 1;
	CopySampleToRam(false);
	// while armed => check for incoming audio
	if ((sampler_mode == SM_ARMED) && (audioin_peak > 1024))
		start_recording_sample();
	if (sampler_mode > SM_ERASING && sampler_mode < SM_STOPPING4) {
		s16* dldst = delaybuf + (buf_write_pos & DLMASK);
		// stopping recording => write zeroes (why don't we just write these all at once?)
		if (sampler_mode >= SM_STOPPING1) {
			memset(dldst, 0, SAMPLES_PER_TICK * 2);
			sampler_mode++;
		}
		// armed or recording => monitor audio (write in-buffer to out-buffer)
		else {
			const s16* asrc = (const s16*)audioin;
			s16* adst = (s16*)dst;
			for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
				s16 smp = *dldst++ = SATURATE16((((int)(asrc[0] + asrc[1])) * (int)(ext_gain_smoother.y2 / 2)) >> 14);
				adst[0] = adst[1] = smp;
				adst += 2;
				asrc += 2;
			}
		}
		buf_write_pos += SAMPLES_PER_TICK;
	}
}

void apply_sample_lpg_noise(u8 voice_id, Voice* voice, float goal_lpg, float noise_diff, float drive, u32* dst) {
	// sampler parameters
	float timestretch = 1.f;
	float posjit = 0.f;
	float sizejit = 1.f;
	float gsize = 0.125f;
	float grate = 1.f;
	float gratejit = 0.f;
	int smppos = 0;
	if (ui_mode != UI_SAMPLE_EDIT) {
		timestretch = param_val_poly(P_SMP_STRETCH, voice_id) * (2.f / 65536.f);
		gsize = param_val_poly(P_SMP_GRAINSIZE, voice_id) * (1.414f / 65536.f);
		grate = param_val_poly(P_SMP_SPEED, voice_id) * (2.f / 65536.f);
		smppos = (param_val_poly(P_SMP_SCRUB, voice_id) * cur_sample_info.samplelen) >> 16;
		posjit = param_val_poly(P_SMP_SCRUB_JIT, voice_id) * (1.f / 65536.f);
		sizejit = param_val_poly(P_SMP_GRAINSIZE_JIT, voice_id) * (1.f / 65536.f);
		gratejit = param_val_poly(P_SMP_SPEED_JIT, voice_id) * (1.f / 65536.f);
	}
	int trig = string_touch_start & (1 << voice_id);

	int prevsliceidx = voice->slice_id;
	bool gp = ui_mode == UI_SAMPLE_EDIT;
	u16 touch_pos = get_string_touch(voice_id)->pos;

	// decide on the sample for the NEXT frame
	if (trig) { // on trigger frames, we FADE out the old grains! then the next dma fetch will be the new sample and
		// we can fade in again
		goal_lpg = 0.f;
		//		DebugLog("\r\n%d", voice_id);
		int ypos = 0;
		if (cur_sample_info.pitched && !gp) {
			/// / / / ////////////////////// multisample choice
			int best = voice_id;
			int bestdist = 0x7fffffff;
			int mypitch = (voice->osc[1].pitch + voice->osc[2].pitch) / 2;
			int mysemi = (mypitch) >> 9;
			static u8 multisampletime[8];
			static u8 trig_count = 0;
			trig_count++;
			for (int i = 0; i < 8; ++i) {
				int dist = abs(mysemi - cur_sample_info.notes[i]) * 256 - (u8)(trig_count - multisampletime[i]);
				if (dist < bestdist) {
					bestdist = dist;
					best = i;
				}
			}
			multisampletime[best] = trig_count; // for round robin
			voice->slice_id = best;
			if (grate < 0.f)
				ypos = 8;
		}
		else {
			voice->slice_id = voice_id;
			ypos = (touch_pos / 256);
			if (gp)
				ypos = 0;
			if (grate < 0.f)
				ypos++;
		}
		voice->touch_pos_start = gp ? 128 : touch_pos;
		// calculate playhead position
		int pos16 = clampi(((voice->slice_id * 8) + ypos) << 10, 0, 65535);
		int i = pos16 >> 13;
		int p0 = cur_sample_info.splitpoints[i];
		int p1 = cur_sample_info.splitpoints[i + 1];
		voice->playhead8 = (p0 << 8) + (((p1 - p0) * (pos16 & 0x1fff)) >> 5);
		if (grate < 0.f) {
			voice->playhead8 -= 192 << 8;
			if (voice->playhead8 < 0)
				voice->playhead8 = 0;
		}
		set_smoother(&voice->touch_pos, 0);
	}
	else { // not trigger - just advance playhead
		float ms2 = (voice->grain_pair[0].multisample_grate
		             + voice->grain_pair[1].multisample_grate); // double multisample rate
		int delta_playhead8 = (int)(grate * ms2 * timestretch * (SAMPLES_PER_TICK * 0.5f * 256.f) + 0.5f);

		int new_playhead = voice->playhead8 + delta_playhead8;

		// if the sample loops and the new playhead has crossed the loop boundary, recalculate new playhead position
		if (cur_sample_info.loop & 1) {
			int loopstart = calcloopstart(voice->slice_id) << 8;
			int loopend = calcloopend(voice->slice_id) << 8;
			int looplen = loopend - loopstart;
			if (looplen > 0 && (new_playhead < loopstart || new_playhead >= loopstart + looplen)) {
				new_playhead = (new_playhead - loopstart) % looplen;
				if (new_playhead < 0)
					new_playhead += looplen;
				new_playhead += loopstart;
			}
		}

		voice->playhead8 = new_playhead;

		float gdeadzone = clampf(minf(1.f - posjit, timestretch * 2.f), 0.f,
		                         1.f); // if playing back normally and not jittering, add a deadzone
		float fpos = deadzone(touch_pos - voice->touch_pos_start, gdeadzone * 32.f);
		if (gp)
			fpos = 0.f;
		smooth_value(&voice->touch_pos, fpos, 2048.f);
	}

	float noise;
	for (int osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		float y1 = voice->lpg_smoother[osc_id].y1;
		float y2 = voice->lpg_smoother[osc_id].y2;
		int randtabpos = rand() & 16383;
		// mix grains
		GrainPair* g = &voice->grain_pair[osc_id];
		int grainidx = voice_id * 4 + osc_id * 2;
		int g0start = 0;
		if (grainidx)
			g0start = grain_buf_end[grainidx - 1];
		int g1start = grain_buf_end[grainidx];
		int g2start = grain_buf_end[grainidx + 1];

		int64_t posa = g->pos[0];
		int64_t posb = g->pos[1];
		int loopstart = calcloopstart(prevsliceidx);
		int loopend = calcloopend(prevsliceidx);
		bool outofrange0 = posa < loopstart || posa >= loopend;
		bool outofrange1 = posb < loopstart || posb >= loopend;
		int gvol24 = g->vol24;
		int dgvol24 = g->dvol24;
		int dpos24 = g->dpos24;
		int fpos24 = g->fpos24;
		float vol = voice->env1_lvl;
		float dvol = (goal_lpg - vol) * (1.f / SAMPLES_PER_TICK);
		outofrange0 |= g1start - g0start <= 2;
		outofrange1 |= g2start - g1start <= 2;
		g->outflags = (outofrange0 ? 1 : 0) + (outofrange1 ? 2 : 0);
		if ((g1start - g0start <= 2 && g2start - g1start <= 2)) {
			// fast mode :) emulate side effects without doing any work
			vol += dvol * SAMPLES_PER_TICK;
			noise += noise_diff * SAMPLES_PER_TICK;
			gvol24 -= dgvol24 * SAMPLES_PER_TICK;
			fpos24 += dpos24 * SAMPLES_PER_TICK;
			int id = fpos24 >> 24;
			g->pos[0] += id;
			g->pos[1] += id;
			fpos24 &= 0xffffff;
		}
		else {
			const s16* src0 = (outofrange0 ? (const s16*)zero : &grain_buf[g0start + 2]) + g->bufadjust;
			const s16* src0_backup = src0;
			const s16* src1 = (outofrange1 ? (const s16*)zero : &grain_buf[g1start + 2]) + g->bufadjust;
			if (spistate && spistate <= grainidx + 2) {
				while (spistate && spistate <= grainidx + 2)
					;
			}
			for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
				int o0, o1;
				u32 ab0 = *(u32*)(src0); // fetch a pair of 16 bit samples to interpolate between
				u32 mix = (fpos24 << (16 - 9)) & 0x7fff0000;
				mix |= 32767 - (mix >> 16); // mix is now the weights for the linear interpolation
				SMUAD(o0, ab0, mix);        // do the interpolation, result is *32768
				o0 >>= 16;

				u32 ab1 = *(u32*)(src1); // fetch a pair for the other grain in the pair
				SMUAD(o1, ab1, mix);     // linear interp by same weights
				o1 >>= 16;

				fpos24 += dpos24; // advance fractional sample pos
				int bigdpos = (fpos24 >> 24);
				fpos24 &= 0xffffff;
				src0 += bigdpos; // advance source pointers by any whole sample increment
				src1 += bigdpos;

				mix = (gvol24 >> 9) & 0x7fff; // blend between the two grain results
				mix |= (32767 - mix) << 16;
				u32 o01 = STEREOPACK(o0, o1);
				int ofinal;
				SMUAD(ofinal, o01, mix);
				gvol24 -= dgvol24;
				if (gvol24 < 0)
					gvol24 = 0;

				s16 n = ((s16*)rndtab)[randtabpos++]; // mix in a white noise source
				noise += noise_diff;                  // volume ramp for noise

				vol += dvol;                                               // volume ramp for grain signal
				float input = (ofinal * drive + n * noise);                // input to filter
				float cutoff = 1.f - squaref(maxf(0.f, 1.f - vol * 1.1f)); // filter cutoff for low pass gate
				y1 += (input - y1) * cutoff;                               // do the lowpass

				int yy = FLOAT2FIXED(y1 * vol, 0);    // for granular, we include an element of straight VCA
				*osc_dst = SATURATE16(*osc_dst + yy); // write to output
				osc_dst += 2;
			}
			int bigposdelta = src0 - src0_backup;
			g->pos[0] += bigposdelta;
			g->pos[1] += bigposdelta;
		} // grain mix
		g->fpos24 = fpos24;
		g->vol24 = gvol24;

		if (gvol24 <= dgvol24 || trig) { // new grain trigger! this is for the *next* frame
			int ph = voice->playhead8 >> 8;
			int slicelen =
			    cur_sample_info.splitpoints[voice->slice_id + 1] - cur_sample_info.splitpoints[voice->slice_id];
			if (ui_mode != UI_SAMPLE_EDIT) {
				ph += ((int)(voice->touch_pos.y2 * slicelen)) >> (10);
				ph += smppos; // scrub input
			}
			g->vol24 = ((1 << 24) - 1);
			int grainsize = ((rand() & 127) * sizejit + 128.f) * (gsize * gsize) + 0.5f;
			grainsize *= SAMPLES_PER_TICK;
			int jitpos = (rand() & 255) * posjit;
			ph += ((grainsize + 8192) * jitpos) >> 8;
			g->dvol24 = g->vol24 / grainsize;

			float grate2 = 1.f + ((rand() & 255) * (gratejit * gratejit)) * (1.f / 256.f);
			if (timestretch < 0.f)
				grate2 = -grate2;
			g->grate_ratio = grate2;
			g->pos[0] = trig ? ph : g->pos[1];
			g->pos[1] = ph;
		}
		voice->lpg_smoother[osc_id].y1 = y1;
		voice->lpg_smoother[osc_id].y2 = y2;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;

	// update pitch (aka dpos24) for next time!
	for (int gi = 0; gi < 2; ++gi) {
		float multisample_grate;
		if (cur_sample_info.pitched && (ui_mode != UI_SAMPLE_EDIT)) {
			int relpitch = voice->osc[1 + gi].pitch - cur_sample_info.notes[voice->slice_id] * 512;
			if (relpitch < -512 * 12 * 5) {
				multisample_grate = 0.f;
			}
			else {
				multisample_grate = // exp2f(relpitch / (512.f * 12.f));
				    table_interp(pitches, relpitch + 32768);
			}
		}
		else {
			multisample_grate = 1.f;
		}
		voice->grain_pair[gi].multisample_grate = multisample_grate;
		int dpos24 = (1 << 24) * (grate * voice->grain_pair[gi].grate_ratio * multisample_grate);
		while (dpos24 > (2 << 24))
			dpos24 >>= 1;
		voice->grain_pair[gi].dpos24 = dpos24;
	}
}

void sort_sample_voices(void) {
	// decide on a priority for 8 voices
	int gprio[8];
	u32 sampleaddr = ((cur_sample_id1 - 1) & 7) * MAX_SAMPLE_LEN;

	for (int i = 0; i < 8; ++i) {
		GrainPair* g = voices[i].grain_pair;
		int glen0 =
		    ((abs(g[0].dpos24) * (SAMPLES_PER_TICK / 2) + g[0].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation
		int glen1 =
		    ((abs(g[1].dpos24) * (SAMPLES_PER_TICK / 2) + g[1].fpos24 / 2 + 1) >> 23) + 2; // +2 for interpolation

		// TODO - if pos at end of next fetch will be out of bounds, negate dpos24 and grate_ratio so we ping pong
		// back for the rest of the grain!
		int glen = maxi(glen0, glen1);
		glen = clampi(glen, 0, AVG_GRAINBUF_SAMPLE_SIZE * 2);
		g[0].bufadjust = (g[0].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
		g[1].bufadjust = (g[1].dpos24 < 0) ? maxi(glen - 2, 0) : 0;
		grain_pos[i * 4 + 0] = (int)(g[0].pos[0]) - g[0].bufadjust + sampleaddr;
		grain_pos[i * 4 + 1] = (int)(g[0].pos[1]) - g[0].bufadjust + sampleaddr;
		grain_pos[i * 4 + 2] = (int)(g[1].pos[0]) - g[1].bufadjust + sampleaddr;
		grain_pos[i * 4 + 3] = (int)(g[1].pos[1]) - g[1].bufadjust + sampleaddr;
		glen += 2; // 2 extra 'samples' for the SPI header
		gprio[i] = ((int)(voices[i].env1_lvl * 65535.f) << 12) + i + (glen << 3);
	}
	sort8(gprio, gprio);
	u8 lengths[8];
	int pos = 0, i;
	for (i = 7; i >= 0; --i) {
		int prio = gprio[i];
		int fi = prio & 7;
		int len = (prio >> 3) & 255;
		// we only budget for MAX_SPI_STATE transfers. so after that, len goes to 0. also helps CPU load
		if (i < 8 - MAX_SAMPLE_VOICES)
			len = 0;
		else if (voices[fi].env1_lvl <= 0.01f && !(string_touched & (1 << fi)))
			len = 0; // if your finger is up and the volume is 0, we can just skip this one.
		lengths[fi] = (pos + len * 4 > GRAINBUF_BUDGET) ? 0 : len;
		pos += len * 4;
	}
	// cumulative sum
	pos = 0;
	for (int i = 0; i < 32; ++i) {
		pos += lengths[i / 4];
		grain_buf_end[i] = pos;
	}
	if (spistate == 0)
		spi_readgrain_dma(0); // kick off the dma for next time
}

// == RECORDING SAMPLES == //

// reset all sample recording variables and initiate erasing the sample flash buffer
void start_erasing_sample_buffer(void) {
	record_flashaddr_base = (edit_sample_id0 & 7) * (2 * MAX_SAMPLE_LEN);
	cur_slice_id = 0;
	buf_start_pos = 0;
	buf_read_pos = 0;
	buf_write_pos = 0;
	init_ext_gain_for_recording();
	sampler_mode = SM_ERASING;
}

void start_recording_sample(void) {
	static const u16 max_leadin = 1024;
	cur_slice_id = 0;
	memset(&cur_sample_info, 0, sizeof(SampleInfo));
	int leadin = mini(buf_write_pos, max_leadin);
	buf_read_pos = buf_start_pos = buf_write_pos - leadin;
	cur_sample_info.samplelen = 0;
	cur_sample_info.splitpoints[0] = leadin;
	sampler_mode = SM_RECORDING;
}

// register a slice point while recording
void sampler_record_slice_point(void) {
	if (cur_sample_info.samplelen >= SAMPLES_PER_TICK) {
		// add slice point if there is still room
		if (cur_slice_id < 7) {
			cur_slice_id++;
			cur_sample_info.splitpoints[cur_slice_id] = cur_sample_info.samplelen - SAMPLES_PER_TICK;
		}
		// when all slices are filled, press ends the recording
		else
			stop_recording_sample();
	}
}

void try_stop_recording_sample(void) {
	if (cur_sample_info.samplelen >= SAMPLES_PER_TICK) {
		if (cur_slice_id > 0)
			cur_slice_id--;
		stop_recording_sample();
	}
}

void finish_recording_sample(void) {
	// clear out the raw audio in the delaybuf
	reverb_clear();
	delay_clear();
	ramtime[GEN_SAMPLE] = millis(); // fill in the remaining split points
	int startsamp = cur_sample_info.splitpoints[cur_slice_id];
	int endsamp = cur_sample_info.samplelen;
	int n = 8 - cur_slice_id;
	for (int i = cur_slice_id + 1; i < 8; ++i) {
		int samp = startsamp + ((endsamp - startsamp) * (i - cur_slice_id)) / n;
		cur_sample_info.splitpoints[i] = samp;
	}
	cur_slice_id = 0;
	ramtime[GEN_SAMPLE] = millis();
	sampler_mode = SM_PREVIEW;
}

// == SLICES == //

static void set_slice_point(u8 slice_id, float slice_pos) {
	float smin = maxf(slice_id ? cur_sample_info.splitpoints[slice_id - 1] + 1024.f : 0.f, 0.f);
	float smax = minf((slice_id < 7) ? cur_sample_info.splitpoints[slice_id + 1] - 1024.f : cur_sample_info.samplelen,
	                  cur_sample_info.samplelen);
	slice_pos = clampf(slice_pos, smin, smax);
	if (cur_sample_info.splitpoints[slice_id] != slice_pos) {
		cur_sample_info.splitpoints[slice_id] = slice_pos;
		ramtime[GEN_SAMPLE] = millis();
	}
}

void sampler_adjust_cur_slice_point(float diff) {
	set_slice_point(cur_slice_id, cur_sample_info.splitpoints[cur_slice_id] + diff);
}

void sampler_adjust_slice_point_from_touch(u8 slice_id, u16 touch_pos, bool init_slice) {
	static s32 start_slice_pos = 0;
	static u16 start_touch_pos = 0;
	static ValueSmoother slice_pos_smoother;

	// save start values
	if (init_slice) {
		cur_slice_id = slice_id;
		start_slice_pos = cur_sample_info.splitpoints[slice_id];
		start_touch_pos = touch_pos;
		set_smoother(&slice_pos_smoother, start_slice_pos);
	}
	// run the pos smoother and set the slice point to the result of it
	else {
		smooth_value(&slice_pos_smoother,
		             start_slice_pos - deadzone(touch_pos - start_touch_pos, 32.f) * (32000.f / 2048.f), 32000.f);
		set_slice_point(slice_id, slice_pos_smoother.y2);
	}
}

void sampler_adjust_cur_slice_pitch(s8 diff) {
	u8 newnote = clampi(cur_sample_info.notes[cur_slice_id] + diff, 0, 96);
	if (newnote != cur_sample_info.notes[cur_slice_id]) {
		cur_sample_info.notes[cur_slice_id] = newnote;
		ramtime[GEN_SAMPLE] = millis();
	}
}

// == MODES == //

void sampler_toggle_play_mode(void) {
	get_sample_info()->pitched = !get_sample_info()->pitched;
	ramtime[GEN_SAMPLE] = millis();
}

void sampler_iterate_loop_mode(void) {
	get_sample_info()->loop = (get_sample_info()->loop + 1) & 3;
	ramtime[GEN_SAMPLE] = millis();
}
