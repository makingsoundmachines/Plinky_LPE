#include "sampler.h"
#include "data/tables.h"
#include "params.h"
#include "strings.h"
#include "ui/ui.h"

// cleanup
extern s16 grainbufend[32];
extern s16 grainbuf[GRAINBUF_BUDGET];
extern volatile u8 spistate;
int param_eval_finger(u8 paramidx, int voice_id, Touch* f);
void knobsmooth_reset(ValueSmoother* s, float ival);
// -- cleanup

#define SMUAD(o, a, b) asm("smuad %0, %1, %2" : "=r"(o) : "r"(a), "r"(b))

// static float smooth_lpg(ValueSmoother* s, s32 out, float drive, float noise, float env1_lvl) {
// 	s16 n = ((s16*)rndtab)[rand() & 16383];
// 	float cutoff = 1.f - squaref(maxf(0.f, 1.f - env1_lvl * 1.1f));
// 	s->y1 += (out * drive + n * noise - s->y1) * cutoff;
// 	return s->y1;
// }

void apply_sample_lpg_noise(u8 voice_id, Voice* voice, Touch* s_touch, float goal_lpg, float noise_diff, float drive,
                            u32* dst) {
	// sampler parameters
	float timestretch = 1.f;
	float posjit = 0.f;
	float sizejit = 1.f;
	float gsize = 0.125f;
	float grate = 1.f;
	float gratejit = 0.f;
	int smppos = 0;
	if (ramsample.samplelen) {
		if (ui_mode != UI_SAMPLE_EDIT) {
			timestretch = param_eval_finger(P_SMP_TIME, voice_id, s_touch) * (2.f / 65536.f);
			gsize = param_eval_finger(P_SMP_GRAINSIZE, voice_id, s_touch) * (1.414f / 65536.f);
			grate = param_eval_finger(P_SMP_RATE, voice_id, s_touch) * (2.f / 65536.f);
			smppos = (param_eval_finger(P_SMP_POS, voice_id, s_touch) * ramsample.samplelen) >> 16;
			posjit = param_eval_finger(P_JIT_POS, voice_id, s_touch) * (1.f / 65536.f);
			sizejit = param_eval_finger(P_JIT_GRAINSIZE, voice_id, s_touch) * (1.f / 65536.f);
			gratejit = param_eval_finger(P_JIT_RATE, voice_id, s_touch) * (1.f / 65536.f);
		}
	}
	int trig = string_touch_start & (1 << voice_id);

	int prevsliceidx = voice->slice_id;
	if (ramsample.samplelen) {
		bool gp = ui_mode == UI_SAMPLE_EDIT;

		// decide on the sample for the NEXT frame
		if (trig) { // on trigger frames, we FADE out the old grains! then the next dma fetch will be the new sample and
			        // we can fade in again
			goal_lpg = 0.f;
			//		DebugLog("\r\n%d", voice_id);
			int ypos = 0;
			if (ramsample.pitched && !gp) {
				/// / / / ////////////////////// multisample choice
				int best = voice_id;
				int bestdist = 0x7fffffff;
				int mypitch = (voice->osc[1].pitch + voice->osc[2].pitch) / 2;
				int mysemi = (mypitch) >> 9;
				static u8 multisampletime[8];
				static u8 trigcount = 0;
				trigcount++;
				for (int i = 0; i < 8; ++i) {
					int dist = abs(mysemi - ramsample.notes[i]) * 256 - (u8)(trigcount - multisampletime[i]);
					if (dist < bestdist) {
						bestdist = dist;
						best = i;
					}
				}
				multisampletime[best] = trigcount; // for round robin
				voice->slice_id = best;
				if (grate < 0.f)
					ypos = 8;
			}
			else {
				voice->slice_id = voice_id;
				ypos = (s_touch->pos / 256);
				if (gp)
					ypos = 0;
				if (grate < 0.f)
					ypos++;
			}
			voice->touch_pos_start = gp ? 128 : s_touch->pos;
			voice->playhead8 = sample_slice_pos8(((voice->slice_id * 8) + ypos) << (16 - 6));
			if (grate < 0.f) {
				voice->playhead8 -= 192 << 8;
				if (voice->playhead8 < 0)
					voice->playhead8 = 0;
			}
			knobsmooth_reset(&voice->touch_pos, 0);
		}
		else { // not trigger - just advance playhead
			float ms2 = (voice->grain_pair[0].multisample_grate
			             + voice->grain_pair[1].multisample_grate); // double multisample rate
			int delta_playhead8 = (int)(grate * ms2 * timestretch * (BLOCK_SAMPLES * 0.5f * 256.f) + 0.5f);
			voice->playhead8 = doloop8(voice->playhead8 + delta_playhead8, voice->slice_id);

			float gdeadzone = clampf(minf(1.f - posjit, timestretch * 2.f), 0.f,
			                         1.f); // if playing back normally and not jittering, add a deadzone
			float fpos = deadzone(s_touch->pos - voice->touch_pos_start, gdeadzone * 32.f);
			if (gp)
				fpos = 0.f;
			smooth_value(&voice->touch_pos, fpos, 2048.f);
		}
	} // sampler prep

	float noise;
	for (int osc_id = 0; osc_id < OSCS_PER_VOICE / 2; osc_id++) {
		s16* osc_dst = ((s16*)dst) + (osc_id & 1);
		noise = voice->noise_lvl;
		float y1 = voice->y[0 + osc_id], y2 = voice->y[2 + osc_id];
		int randtabpos = rand() & 16383;
		// mix grains
		GrainPair* g = &voice->grain_pair[osc_id];
		int grainidx = voice_id * 4 + osc_id * 2;
		int g0start = 0;
		if (grainidx)
			g0start = grainbufend[grainidx - 1];
		int g1start = grainbufend[grainidx];
		int g2start = grainbufend[grainidx + 1];

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
		float dvol = (goal_lpg - vol) * (1.f / BLOCK_SAMPLES);
		outofrange0 |= g1start - g0start <= 2;
		outofrange1 |= g2start - g1start <= 2;
		g->outflags = (outofrange0 ? 1 : 0) + (outofrange1 ? 2 : 0);
		if ((g1start - g0start <= 2 && g2start - g1start <= 2)) {
			// fast mode :) emulate side effects without doing any work
			vol += dvol * BLOCK_SAMPLES;
			noise += noise_diff * BLOCK_SAMPLES;
			gvol24 -= dgvol24 * BLOCK_SAMPLES;
			fpos24 += dpos24 * BLOCK_SAMPLES;
			int id = fpos24 >> 24;
			g->pos[0] += id;
			g->pos[1] += id;
			fpos24 &= 0xffffff;
		}
		else {
			const s16* src0 = (outofrange0 ? (const s16*)zero : &grainbuf[g0start + 2]) + g->bufadjust;
			const s16* src0_backup = src0;
			const s16* src1 = (outofrange1 ? (const s16*)zero : &grainbuf[g1start + 2]) + g->bufadjust;
			if (spistate && spistate <= grainidx + 2) {
				while (spistate && spistate <= grainidx + 2)
					;
			}
			for (int i = 0; i < BLOCK_SAMPLES; ++i) {
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
			int slicelen = ramsample.splitpoints[voice->slice_id + 1] - ramsample.splitpoints[voice->slice_id];
			if (ui_mode != UI_SAMPLE_EDIT) {
				ph += ((int)(voice->touch_pos.y2 * slicelen)) >> (10);
				ph += smppos; // scrub input
			}
			g->vol24 = ((1 << 24) - 1);
			int grainsize = ((rand() & 127) * sizejit + 128.f) * (gsize * gsize) + 0.5f;
			grainsize *= BLOCK_SAMPLES;
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
		voice->y[osc_id] = y1, voice->y[osc_id + 2] = y2;
	} // osc loop

	voice->env1_lvl = goal_lpg;
	voice->noise_lvl = noise;

	// update pitch (aka dpos24) for next time!
	for (int gi = 0; gi < 2; ++gi) {
		float multisample_grate;
		if (ramsample.pitched && (ui_mode != UI_SAMPLE_EDIT)) {
			int relpitch = voice->osc[1 + gi].pitch - ramsample.notes[voice->slice_id] * 512;
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
