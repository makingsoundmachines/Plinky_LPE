#include "gfx/data/names.h"
#include "gfx/gfx.h"
#include "hardware/encoder.h"
#include "synth/pitch_tools.h"
#include "synth/sampler.h"
#include "synth/strings.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

u8 audiohistpos = 0;
u8 audiopeakhistory[32];
u8 erasepos = 0;
u8 messagefnt;
const char* message = 0;
const char* submessage = 0;
u32 messagetime = 0;
void ShowMessage(Font fnt, const char* msg, const char* submsg) {
	message = msg;
	submessage = submsg;
	messagefnt = fnt;
	messagetime = millis() + 500;
}

char
#ifndef EMU
    __attribute__((section(".endsection")))
#endif
    version_tail[] = VERSION2;

void flip(void) {
	if (millis() > messagetime)
		message = 0;
	else if (message) {
		oled_clear();
		int y = 0;
		if (submessage)
			draw_str(0, 0, F_12, submessage), y += 12;
		draw_str(0, y, messagefnt, message);
	}
	static u8 frame = 3;
	if (frame < 255) {
		// draw version
		int y = frame - 255 + 32;
		if (y < 32 - 12)
			y = 32 - 12;
		gfx_text_color = 3;
		fdraw_str(32, y, F_12,
		          VERSION2
#ifdef DEBUG
		          " DBG"
#endif
#ifndef NEW_LAYOUT
		          " OL"
#endif
		          ,
		          version_tail // version tail is passed to printf, so the linker cant remove it, haha, but we dont
		                       // actually use the pointer
		);
		if (frame < 36)
			gfx_dither_logo(frame);
		frame += 4;
	}
	gfx_text_color = 1;
	oled_flip();
}

void draw_recording_ui(void) {
	oled_clear();
	SampleInfo* s = get_sample_info();

	int peak = maxi(0, audioin_peak / 128);
	int hold = maxi(0, audioin_hold / 128);
	if (sampler_mode == SM_ERASING) {
		draw_str(0, 0, F_32, "erasing...");
		inverted_rectangle(0, 0, erasepos * 2, 32);
		flip();
		return;
	}
	else {
		draw_str(-128, 0, F_12,
		         (sampler_mode == SM_ARMED)       ? "armed!"
		         : (sampler_mode == SM_RECORDING) ? "recording"
		                                          : "rec level " I_A);
		fdraw_str(-128, 32 - 12, F_12, (hold >= 254) ? "CLIP! %dms" : "%dms", s->samplelen / 32);
	}
	int full = s->samplelen / (MAX_SAMPLE_LEN / 128);
	if (sampler_mode != SM_RECORDING)
		full = 0;
	for (int i = 0; i < full; ++i) {
		u16 avgpeak = getwaveform4zoom(s, i * 16, 4);
		u8 avg = avgpeak;
		u8 peak = avgpeak >> 8;
		vline(i, 15 - avg, 16 + avg, 1);
		vline(i, 15 - peak, 15 - avg, 2);
		vline(i, 16 + avg, 16 + peak, 2);
	}
	vline(full, 0, 32, 1);
	int srcpos = buf_write_pos;
	for (int i = 127; i > full; --i) {
		int pmx = 0, pmn = 0;
		for (int j = 0; j < 256; ++j) {
			int p = -delaybuf[--srcpos & DLMASK];
			pmx = maxi(p, pmx);
			pmn = mini(p, pmn);
		}
		vline(i, 15 + pmn / 1024, 16 + pmx / 1024, 2);
	}
	fill_rectangle(hold - 1, 29, hold + 1, 32);
	half_rectangle(peak, 29, hold, 32);
	fill_rectangle(0, 29, peak, 32);
	flip();
}

const char* notename(int note) {
	// blondi asked to octave-up the displayed notes
	note += 12;
	if (note < 0 || note > 8 * 12)
		return "";
	static char buf[4];
	int octave = note / 12;
	note -= octave * 12;
	buf[0] = "CCDDEFFGGAAB"[note];
	buf[1] = " + +  + + + "[note];
	buf[2] = '0' + octave;
	return buf;
}

void DebugSPIPage(int addr) {
	while (spistate)
		;
	spistate = 255;

	int spiid = spi_readid();
	DebugLog("SPI flash chip id %04x\r\n", spiid);

	for (int i = 0; i < 256; ++i) {
		memset(spibigrx, -2, sizeof(spibigrx));
		spi_read256(i * 256 + addr);
		memcpy(&delaybuf[i * 128], spibigrx + 4, 256);
	}
	DebugLog("first 256 samples of spi ram at addr %d:\r\n", addr);
	for (int i = 0; i < 256; ++i) {
		DebugLog("%5d ", delaybuf[i]);
		if ((i & 31) == 31)
			DebugLog("\r\n");
	}
	spistate = 0;
}

void DrawSample(SampleInfo* s, int slice_id) {
	slice_id &= 7;
	int ofs = s->splitpoints[slice_id] / 1024;
	int maxx = s->samplelen / 1024;
	gfx_text_color = 3;
	for (int i = 0; i < 128; ++i) {
		int x = i - 64 + ofs;
		u8 h = getwaveform4(s, x);
		if (x >= 0 && x < maxx)
			vline(i, 15 - h, 16 + h, (i < 64) ? 2 : 1);
		if (i == 64) {
			vline(i, 0, 13 - h, 1);
			vline(i, 18 + h, 32, 1);
		}
	}
	fdraw_str(64 + 2, 0, F_12, "%d", slice_id + 1);
	// draw other slices
	for (int si = 0; si < 8; ++si)
		if (si != slice_id) {
			int x = (s->splitpoints[si] / 1024) - ofs + 64;
			char buf[2] = {'1' + si, 0};
			u8 h = getwaveform4(s, s->splitpoints[si] / 1024);
			if (x >= 0 && x < 128) {
				vline(x, 0, 13 - h, 2);
				vline(x, 18 + h, 32, 2);
			}
			drawstr_noright(x + 2, 0, F_8, buf);
		}
}

void DrawSamplePlayback(SampleInfo* s) {
	static int curofscenter = 0;
	static bool jumpable = false;
	int ofs = curofscenter / 1024;
	gfx_text_color = 3;
	for (int i = 32; i < 128 - 16; ++i) {
		int x = i - 32 + ofs;
		u8 h = getwaveform4(s, x);
		vline(i, 15 - h, 16 + h, 2);
	}
	for (int si = 0; si < 8; ++si) {
		int x = (s->splitpoints[si] / 1024) - ofs + 32;
		u8 h = getwaveform4(s, s->splitpoints[si] / 1024);
		if (x >= 32 && x < 128 - 16) {
			vline(x, 0, 13 - h, 1);
			vline(x, 18 + h, 32, 1);
		}
	}
	s8 gx[8 * 4], gy[8 * 4], gd[8 * 4];
	int numtodraw = 0;
	int min_pos = MAX_SAMPLE_LEN;
	int max_pos = 0;

	min_pos = clampi(min_pos, 0, s->samplelen);
	max_pos = clampi(max_pos, 0, s->samplelen);

	for (int i = 0; i < 8; ++i) {
		GrainPair* gr = voices[i].grain_pair;
		int vvol = (int)(256.f * voices[i].env1_lvl);
		if (vvol > 8)
			for (int g = 0; g < 4; ++g) {
				if (!(gr->outflags & (1 << (g & 1)))) {
					int pos = grain_pos[i * 4 + g] & (MAX_SAMPLE_LEN - 1);
					int vol = gr->vol24 >> (24 - 4);
					if (g & 1)
						vol = 15 - vol;
					int graindir = (gr->dpos24 < 0) ? -1 : 1;
					int disppos = pos + graindir * 1024 * 32;
					min_pos = mini(min_pos, pos);
					max_pos = maxi(max_pos, pos);
					min_pos = mini(min_pos, disppos);
					max_pos = maxi(max_pos, disppos);
					int x = (pos / 1024) - ofs + 32;
					int y = (vol * vvol) >> 8;
					if (g & 2)
						y = 16 + y;
					else
						y = 16 - y;
					if (x >= 32 && x < 128 - 16 && y >= 0 && y < 32) {
						gx[numtodraw] = x;
						gy[numtodraw] = y;
						gd[numtodraw] = graindir;
						numtodraw++;
					}
				}
				if (g & 1)
					gr++;
			}
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 2, y + 2, 0);
		vline(x + gd[i], y - 1, y + 2, 0);
	}
	for (int i = 0; i < numtodraw; ++i) {
		int x = gx[i], y = gy[i];
		vline(x, y - 1, y + 1, 1);
		put_pixel(x + gd[i], y, 1);
	}
	if (min_pos >= max_pos)
		jumpable = true;
	else {
		if (jumpable)
			curofscenter = min_pos - 8 * 1024;
		else {
#define GRAIN_SCROLL_SHIFT 3
			if (min_pos < curofscenter)
				curofscenter += (min_pos - curofscenter) >> GRAIN_SCROLL_SHIFT;
			if (max_pos > curofscenter + (128 - 48) * 1024)
				curofscenter += (max_pos - curofscenter - (128 - 48) * 1024) >> GRAIN_SCROLL_SHIFT;
		}
		jumpable = false;
	}
}

const bool pre_erase = true;
u32 record_flashaddr_base = 0;

void samplemode_ui(void) {
	SampleInfo* s = get_sample_info();
	if (sampler_mode == SM_ERASING) {
		erasepos = 0;
		draw_recording_ui();
		while (spistate)
			;
		spistate = 255;
		HAL_Delay(10);
		// mysteriously, sometimes page 0 wasn't erasing. maybe do it twice? WOOAHAHA
		spi_erase64k(0 + record_flashaddr_base, draw_recording_ui);
		for (int addr = 0; addr < MAX_SAMPLE_LEN * 2; addr += 65536) {
			spi_erase64k(addr + record_flashaddr_base, draw_recording_ui);
			erasepos++;
		}
		memset(s, 0, sizeof(SampleInfo)); // since we erased the ram, we might as well nuke the sample too
		ramtime[GEN_SAMPLE] = millis();
		// done!
		spistate = 0;
		sampler_mode = SM_PRE_ARMED;
	}
	if (sampler_mode == SM_PREVIEW) {
		if (shift_state_frames > 4 && (shift_state == SS_RECORD) && sampler_mode == SM_PREVIEW) {
			oled_clear();
			draw_str(0, 0, F_32, "record?");
			inverted_rectangle(0, 0, shift_state_frames * 2, 32);
			oled_flip();
			return;
		}
		// just show the waveform of the current slice
		oled_clear();
		if (!s->samplelen) {
			draw_str(0, 0, F_16, "<empty sample>");
			draw_str(0, 16, F_16, "hold " I_RECORD " to record");
		}
		else {
			DrawSample(s, cur_slice_id);
			gfx_text_color = 2;
			draw_str(-128 + 16, 32 - 12, F_12, (s->loop & 2) ? "all" : "slc");
			draw_icon(128 - 16, 32 - 14, ((s->loop & 1) ? I_FEEDBACK[0] : I_RIGHT[0]) - 0x80, gfx_text_color);
			if (s->pitched)
				draw_str(0, 32 - 12, F_12, notename(s->notes[cur_slice_id & 7]));
			else
				draw_str(0, 32 - 12, F_12, "tape");
			gfx_text_color = 1;
		}
		flip();
		for (int x = 0; x < 8; ++x) {
			int sp0 = s->splitpoints[x];
			int sp1 = (x < 7) ? s->splitpoints[x + 1] : s->samplelen;
			for (int y = 0; y < 8; ++y) {
				int samp = sp0 + (((sp1 - sp0) * y) >> 3);
				const static int zoom = 3;
				u16 avgpeak = getwaveform4zoom(s, samp / 1024, zoom);
				u8 h = avgpeak & 15;
				leds[x][y] = led_add_gamma(h * 32);
			}
			if (x == cur_slice_id) {
				leds[x][0] = triangle(millis());
			}
			leds[8][x] = 0;
		}
		leds[8][SS_RECORD] = triangle(millis());
		leds[8][SS_PLAY] = 0;
		leds[8][SS_SHIFT_A] = (s->pitched ? 255 : 0);
		leds[8][SS_SHIFT_B] = ((s->loop & 1) ? 255 : 0);
		return;
	}

	// block of data handling - belongs in memory module
	if (sampler_mode >= SM_RECORDING) {

		int locrecpos = buf_write_pos;
		while (spistate)
			;
		spistate = 0xff; // prevent spi reads for a while!
		while (buf_read_pos + 256 / 2 <= locrecpos && s->samplelen < MAX_SAMPLE_LEN) {
			s16* src = delaybuf + (buf_read_pos & DLMASK);
			s16* dst = (s16*)(spibigtx + 4);
			int flashaddr = (buf_read_pos - buf_start_pos) * 2;
			buf_read_pos += 256 / 2;
			if (!pre_erase && (flashaddr & 65535) == 0)
				spi_erase64k(flashaddr + record_flashaddr_base, /*draw_recording_ui*/ 0);
			// find peak and copy to delay buf
			int peak = 0;
			s16* delaybufend = delaybuf + DLMASK + 1;
			for (int i = 0; i < 256 / 2; ++i) {
				s16 smp = *src++;
				*dst++ = smp;
				peak = maxi(peak, abs(smp));
				if (src == delaybufend)
					src = delaybuf;
			}
			setwaveform4(s, flashaddr / 2 / 1024, peak / 1024);
			if (spi_write256(flashaddr + record_flashaddr_base) != 0) {
				DebugLog("flash write fail\n");
			}
			s->samplelen = buf_read_pos - buf_start_pos;
			ramtime[GEN_SAMPLE] = millis();
			// sample full => stop recording
			if (s->samplelen >= MAX_SAMPLE_LEN) {
				stop_recording_sample();
				break;
			}
		} // spi write loop

		spistate = 0;
		if (sampler_mode == SM_STOPPING4) {
			finish_recording_sample();
		}
	}

	int bufsize = buf_write_pos - buf_read_pos;
	if (bufsize < 4096 || sampler_mode != SM_RECORDING) {
		draw_recording_ui();
	}
	/////////// update leds

	for (int x = 0; x < 8; ++x) {
		int barpos0 = audiopeakhistory[(audiohistpos + x * 4 + 1) & 31];
		int barpos1 = audiopeakhistory[(audiohistpos + x * 4 + 2) & 31];
		int barpos2 = audiopeakhistory[(audiohistpos + x * 4 + 3) & 31];
		int barpos3 = audiopeakhistory[(audiohistpos + x * 4 + 4) & 31];
		for (int y = 0; y < 8; ++y) {
			int yy = (7 - y) * 32;
			int k = clampi(barpos0 - yy, 0, 31);
			k += clampi(barpos1 - yy, 0, 31);
			k += clampi(barpos2 - yy, 0, 31);
			k += clampi(barpos3 - yy, 0, 31);
			leds[x][y] = led_add_gamma(k * 2);
		}
		if (x == cur_slice_id && sampler_mode == SM_RECORDING) {
			for (int y = 0; y < 8; ++y)
				leds[x][y] = maxi(leds[x][y], (triangle(millis()) * 4) / (y + 4));
		}
		leds[8][x] = 0;
	}
	leds[8][SS_RECORD] = (sampler_mode == SM_RECORDING) ? 255 : triangle(millis() / 2);
}

const static float life_damping = 0.91f; //  0.9f;
const static float life_force = 0.25f;   // 0.25f;
const static float life_input_power = 6.f;

static int frame = 0;

void DrawLFOs(void) {
	u8* vr = oled_buffer();
	vr += 128 - 16;
	u8 lfohp = ((lfo_history_pos >> 4) + 1) & 15;
	for (int x = 0; x < 16; ++x) {
		vr[0] &= ~(lfo_history[lfohp][0] >> 1);
		vr[128] &= ~(lfo_history[lfohp][1] >> 1);
		vr[256] &= ~(lfo_history[lfohp][2] >> 1);
		vr[384] &= ~(lfo_history[lfohp][3] >> 1);

		vr[0] |= lfo_history[lfohp][0];
		vr[128] |= lfo_history[lfohp][1];
		vr[256] |= lfo_history[lfohp][2];
		vr[384] |= lfo_history[lfohp][3];
		vr++;
		lfohp = (lfohp + 1) & 15;
	}
}

void DrawVoices(void) {
	const static u8 leftOffset = 44;
	const static u8 maxHeight = 10;
	const static u8 barWidth = 3;
	const static float moveSpeed = 5; // pixels per frame
	static float touchLineHeight[8];
	static float maxVolume[8];
	static float volLineHeight[8];
	u8 rightOffset = (rampreset.flags & FLAGS_LATCH) ? 38 : 14;
	// all voices
	for (u8 i = 0; i < 8; i++) {
		// string volume
		if (maxVolume[i] != 0) {
			volLineHeight[i] = voices[i].env1_lvl / maxVolume[i] * maxHeight;
		}
		// string pressed
		if (write_string_touched_copy & (1 << i)) {
			// move touch line
			if (touchLineHeight[i] < maxHeight)
				touchLineHeight[i] += moveSpeed;
			if (touchLineHeight[i] > maxHeight)
				touchLineHeight[i] = maxHeight;
			// volume line catches up
			volLineHeight[i] = maxf(volLineHeight[i], touchLineHeight[i]);
			// remember peak volume
			maxVolume[i] = voices[i].env1_lvl;
		}
		// string not pressed
		else {
			if (touchLineHeight[i] > 0)
				touchLineHeight[i] -= moveSpeed;
			if (touchLineHeight[i] < 0)
				touchLineHeight[i] = 0;
			// has the sound died out?
			if (maxVolume[i] != 0 && volLineHeight[i] < 0.25) {
				// disable volume line
				maxVolume[i] = 0;
				volLineHeight[i] = 0;
			}
		}
		// draw bars
		u8 x = i * (OLED_WIDTH - leftOffset - rightOffset) / 8 + leftOffset;
		if (touchLineHeight[i] > 0) {
			for (uint8_t dx = 0; dx < barWidth; dx++)
				vline(x - barWidth / 2 + dx, OLED_HEIGHT - 1 - touchLineHeight[i], OLED_HEIGHT - 1, 2);
		}
		hline(x - barWidth / 2, OLED_HEIGHT - 1 - volLineHeight[i], x - barWidth / 2 + barWidth, 1);
	}
}

void DrawFlags() {
	gfx_text_color = 0;
	if ((rampreset.flags & FLAGS_ARP)) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
	}
	if ((rampreset.flags & FLAGS_LATCH)) {
		fill_rectangle(128 - 38, 32 - 8, 128 - 17, 32);
		draw_str(-(128 - 17), 32 - 7, F_8, "latch");
	}
	gfx_text_color = 1;
	vline(126, 32 - (synth_max_pres >> 6), 32, 1);
	vline(127, 32 - (synth_max_pres >> 6), 32, 1);
}

const char* getparamstr(int p, int mod, int v, char* valbuf, char* decbuf) {
	if (decbuf)
		*decbuf = 0;
	int valmax = param_flags[p] & FLAG_MASK;
	int vscale = valmax ? (mini(v, FULL - 1) * valmax) / FULL : v;
	int displaymax = valmax ? valmax * 10 : 1000;
	bool decimal = true;
	//	const char* val = valbuf;
	if (mod == M_BASE)
		switch (p) {
		case P_SMP_TIME:
		case P_SMP_RATE:
			displaymax = 2000;
			break;
		case P_ARPONOFF:
			if (mod)
				return "";
			return ((rampreset.flags & FLAGS_ARP)) ? "On" : "Off";
		case P_LATCHONOFF:
			if (mod)
				return "";
			return ((rampreset.flags & FLAGS_LATCH)) ? "On" : "Off";
		case P_SAMPLE:
			if (vscale == 0) {
				return "Off";
			}
			break;
		case P_SEQDIV: {
			if (vscale >= DIVISIONS_MAX)
				return "(Gate CV)";
			int n = sprintf(valbuf, "%d", divisions[vscale] /* >> divisor*/);
			if (!decbuf)
				decbuf = valbuf + n;
			sprintf(decbuf, /*divisornames[divisor]*/ "/32");
			return valbuf;
		}
		case P_ARPDIV:
			if (v < 0) {
				v = -v;
				decimal = true;
				valmax = 0;
				displaymax = 1000;
				break;
			}
			vscale = (mini(v, FULL - 1) * DIVISIONS_MAX) / FULL;
			int n = sprintf(valbuf, "%d", divisions[vscale] /*>> divisor*/);
			if (!decbuf)
				decbuf = valbuf + n;
			sprintf(decbuf, /*divisornames[divisor]*/ "/32");
			return valbuf;
		case P_ARPOCT:
			v += (FULL * 10) / displaymax; // 1 based
			break;
		case P_MIDI_CH_IN:
		case P_MIDI_CH_OUT: {
			int midich = clampi(vscale, 0, 15) + 1;
			int n = sprintf(valbuf, "%d", midich);
			if (!decbuf)
				decbuf = valbuf + n;
			return valbuf;
		}
		case P_ARPMODE:
			return arpmodenames[clampi(vscale, 0, ARP_LAST - 1)];
		case P_SEQMODE:
			return seqmodenames[clampi(vscale, 0, SEQ_LAST - 1)];
		case P_CV_QUANT:
			return cvquantnames[clampi(vscale, 0, CVQ_LAST - 1)];
		case P_SCALE:
			return scalenames[clampi(vscale, 0, S_LAST - 1)];
		case P_ASHAPE:
		case P_BSHAPE:
		case P_XSHAPE:
		case P_YSHAPE:
			return lfonames[clampi(vscale, 0, LFO_LAST - 1)];
		case P_PITCH:
		case P_INTERVAL:
			displaymax = 120;
			break;
		case P_TEMPO:
			v += FULL;
			if (external_clock_enable)
				v = (bpm10x * FULL) / 1200;
			displaymax = 1200;
			break;
		case P_DLTIME:
			if (v < 0) {
				if (v <= -1024)
					v++;
				v = (-v * 13) / FULL;
				int n = sprintf(valbuf, "%d", divisions[v]);
				if (!decbuf)
					decbuf = valbuf + n;
				sprintf(decbuf, "/32 sync");
			}
			else {
				int n = sprintf(valbuf, "%d", (v * 100) / FULL);
				if (!decbuf)
					decbuf = valbuf + n;
				sprintf(decbuf, "free");
			}
			return valbuf;
		default:;
		}
	v = (v * displaymax) / FULL;
	int av = abs(v);
	int n = sprintf(valbuf, "%c%d", (v < 0) ? '-' : ' ', av / 10);
	if (decimal) {
		if (!decbuf)
			decbuf = valbuf + n;
		sprintf(decbuf, ".%d", av % 10);
	}
	return valbuf;
}

void edit_mode_ui(void) {

	oled_clear();

	float damping = life_damping;
	float force = life_force;
	float* prev = surf[frame & 1][0];
	frame++;
	float* next = surf[frame & 1][0];
	int i = 0;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x, ++i) {
			Touch* curfinger = get_string_touch(x);
			float corners = 0.f, edges = 0.f;
			if (x > 0) {
				if (y > 0)
					corners += prev[i - 9];
				edges += prev[i - 1];
				if (y < 7)
					corners += prev[i + 7];
			}
			if (y > 0)
				edges += prev[i - 8];
			if (y < 7)
				edges += prev[i + 8];
			if (x < 7) {
				if (y > 0)
					corners += prev[i - 7];
				edges += prev[i + 1];
				if (y < 7)
					corners += prev[i + 9];
			}
			float target = corners * (1.f / 12.f) + edges * (1.f * 2.f / 12.f);
			target *= damping;
			if (curfinger->pos >> 8 == y) {
				float pressure = curfinger->pres * (1.f / 2048.f);
				if ((rampreset.flags & FLAGS_ARP) && !(arpbits & (1 << x)))
					pressure = 0.f;

				target = lerp(target, life_input_power, clampf(pressure * 2.f, 0.f, 1.f));
			}
			float pos = prev[i];
			float accel = (target - pos) * force;
			float vel = (prev[i] - next[i]) * damping + accel; // here next is really 'prev prev'
			next[i] = pos + vel;
		} // x
	} // y

	char presetname[9];
	memcpy(presetname, rampreset.name, 8);
	presetname[8] = 0;

	u8* vr = oled_buffer();
	static u8 ui_edit_param = P_LAST;
	u8 ep = selected_param;
	if (ui_edit_param != ep) {
		ui_edit_param = ep; // as it may change in the background
	}
	u8 ui_edit_mod = selected_mod_src;
	u8 loopstart_step = (rampreset.loopstart_step_no_offset + step_offset) & 63;

	const char* pagename = 0;
	if (shift_state == SS_RECORD) {
		if (shift_short_pressed())
			draw_str(0, 4, F_20_BOLD, recording ? I_RECORD "record >off" : I_RECORD "record >on");
		else if (recording) {
			if (recording_knobs == 0)
				draw_str(0, 4, F_20_BOLD, "record " I_A I_B "?");
			else if (recording_knobs == 1)
				draw_str(0, 4, F_20_BOLD, "recording " I_A);
			else if (recording_knobs == 2)
				draw_str(0, 4, F_20_BOLD, "recording " I_B);
			else if (recording_knobs == 3)
				draw_str(0, 4, F_20_BOLD, "recording " I_A I_B);
		}
	}
	else if (shift_state == SS_PLAY) {
		draw_str(0, 0, F_32_BOLD, I_PLAY "play");
	}
	else if (shift_state == SS_CLEAR && ui_mode != UI_LOAD) {
		draw_str(0, 0, F_32_BOLD, I_CROSS "clear");
	}
	else
		switch (ui_mode) {
		case UI_DEFAULT:
			if (using_sampler())
				DrawSamplePlayback(get_sample_info());
			else {
				for (int x = 0; x < 128; ++x) {
					u32 m = scope[x];
					vr[0] = m;
					vr[128] = m >> 8;
					vr[256] = m >> 16;
					vr[384] = m >> 24;
					vr++;
				}
			}
			DrawLFOs();
			DrawVoices();
			gfx_text_color = 2;
			if (ui_edit_param < P_LAST)
				goto draw_parameter;

			if (last_selected_param < P_LAST && enc_recently_used())
				goto draw_parameter;
			DrawFlags();
			char seqicon = (rampreset.flags & FLAGS_ARP) ? I_NOTES[0] : I_SEQ[0];
			char preseticon = I_PRESET[0];
			int xtab = 0;
			if (pending_preset != 255 && pending_preset != sysparams.curpreset)
				xtab = fdraw_str(0, 0, F_20_BOLD, "%c%d->%d", preseticon, sysparams.curpreset + 1, pending_preset + 1);
			else if (synth_max_pres > 1 && !(using_sampler() && !get_sample_info()->pitched)) {
				xtab = fdraw_str(0, 0, F_20_BOLD, "%s", notename((high_string_pitch + 1024) / 2048));
			}
			else
				xtab = fdraw_str(0, 0, F_20_BOLD, "%c%d", preseticon, sysparams.curpreset + 1);
			draw_str(xtab + 2, 0, F_8_BOLD, presetname);
			if (rampreset.category > 0 && rampreset.category < CAT_LAST)
				draw_str(xtab + 2, 8, F_8, kpresetcats[rampreset.category]);
			if (pending_pattern != 255 && pending_pattern != cur_pattern)
				fdraw_str(0, 16, F_20_BOLD, "%c%d->%d", seqicon, cur_pattern + 1, pending_pattern + 1);
			else
				fdraw_str(0, 16, F_20_BOLD, "%c%d", seqicon, cur_pattern + 1);
			break;
		case UI_EDITING_A:
		case UI_EDITING_B:
			if (ui_edit_param >= P_LAST) {
				draw_str(0, 0, F_20_BOLD, modnames[ui_edit_mod]);
				draw_str(0, 16, F_16, "select parameter");
				break;
			}
			else {
				DrawLFOs();
				int pi;
draw_parameter:
				pi = ui_edit_param;
				if (pi >= P_LAST)
					pi = last_selected_param;
				if (pi >= P_LAST)
					pi = 0;
				pagename = pagenames[pi / 6];
				switch (pi) {
				case P_TEMPO:
					pagename = I_TEMPO "Tap";
					break;
#ifndef NEW_LAYOUT
				case P_ENV_LEVEL:
				case P_ENV_REPEAT:
				case P_ENV_WARP:
				case P_ENV_RATE:
					pagename = "env2";
					break;
#endif
				case P_NOISE:
					pagename = I_WAVE "noise";
					break;
				case P_CV_QUANT:
				case P_HEADPHONE:
					pagename = "system";
					break;
				}
				draw_str(0, 0, F_12, (ui_edit_mod == 0) ? pagename : modnames[ui_edit_mod]);
				const char* pn = paramnames[pi];
				int pw = str_width(F_16_BOLD, pn);
				if (pw > 64)
					draw_str(0, 20, F_12_BOLD, pn);
				else
					draw_str(0, 16, F_16_BOLD, pn);
				char valbuf[32];
				char decbuf[16];
				int w = 0;
				int v = GetParam(pi, ui_edit_mod);
				int vbase = v;
				if (ui_edit_mod == 0) {
					v = (param_eval_int_noscale(pi, any_rnd, env16, pressure16) * FULL) >> 16;
					if (v != vbase) {
						// if there is modulation going on, show the base value below
						const char* val = getparamstr(pi, ui_edit_mod, vbase, valbuf, NULL);
						w = str_width(F_8, val);
						draw_str(128 - 16 - w, 32 - 8, F_8, val);
					}
				}
				const char* val = getparamstr(pi, ui_edit_mod, v, valbuf, decbuf);
				int x = 128 - 15;
				if (*decbuf)
					x -= str_width(F_8, decbuf);
				int font = F_24_BOLD;
				while (1) {
					w = str_width(font, val);
					if (w < 64 || font <= F_12_BOLD)
						break;
					font--;
				}
				draw_str(x - w, 0, font, val);
				if (*decbuf)
					draw_str(x, 0, F_8, decbuf);
			}
			break;
		case UI_PTN_START:
			fdraw_str(0, 0, F_20_BOLD, I_PREV "Start %d", loopstart_step + 1);
			fdraw_str(0, 16, F_20_BOLD, I_PLAY "Current %d", cur_step + 1);
			break;
		case UI_PTN_END:
			fdraw_str(0, 0, F_20_BOLD, I_NEXT "End %d", ((rampreset.looplen_step + loopstart_step) & 63) + 1);
			fdraw_str(0, 16, F_20_BOLD, I_INTERVAL "Length %d", rampreset.looplen_step);
			break;
		case UI_LOAD:
			if (shift_state_frames > 4 && shift_state == SS_CLEAR) {
				bool done = (shift_state_frames - 4) > 64;
				if (selected_preset_global < 32)
					fdraw_str(0, 0, F_16_BOLD,
					          done ? "cleared\n" I_PRESET "Preset %d" : "initialize\n" I_PRESET "Preset %d?",
					          selected_preset_global + 1);
				else if (selected_preset_global < 64 - 8)
					fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_SEQ "Pattern %d." : "Clear\n" I_SEQ "Pattern %d?",
					          selected_preset_global - 32 + 1);
				else if (selected_preset_global < 64 && selected_preset_global > 0)
					fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_WAVE "Sample %d." : "Clear\n" I_WAVE "Sample %d?",
					          selected_preset_global - (64 - 8) + 1);
				inverted_rectangle(0, 0, shift_state_frames * 2 - 4, 32);
			}
			else if (long_press_frames >= 32) {
				bool done = (long_press_frames - 32) > 128;
				if (long_press_pad >= 56)
					fdraw_str(0, 0, F_16_BOLD, done ? "ok!" : "Edit\n" I_WAVE "Sample %d?", long_press_pad - 56 + 1);
				else if (long_press_pad == preset_copy_source)
					fdraw_str(0, 0, F_16_BOLD,
					          done ? "toggled\n" I_PRESET "Preset %d" : "toggle\n" I_PRESET "Preset %d?",
					          preset_copy_source + 1);
				else if (long_press_pad < 32)
					fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_PRESET "%d" : "copy over\n" I_PRESET "Preset %d?",
					          long_press_pad + 1);
				else
					fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_SEQ "%d" : "copy over\n" I_SEQ "Pat %d?",
					          long_press_pad - 32 + 1);

				inverted_rectangle(0, 0, long_press_frames - 32, 32);
			}
			else {
				int xtab = fdraw_str(0, 0, F_20_BOLD, I_PRESET "%d", sysparams.curpreset + 1);
				draw_str(xtab + 2, 0, F_8_BOLD, presetname);
				if (rampreset.category > 0 && rampreset.category < CAT_LAST)
					draw_str(xtab + 2, 8, F_8, kpresetcats[rampreset.category]);

				fdraw_str(0, 16, F_20_BOLD, I_SEQ "Pat %d ", cur_pattern + 1);
				fdraw_str(-128, 16, F_20_BOLD, cur_sample_id1 ? I_WAVE "%d" : I_WAVE "Off", cur_sample_id1);
			}
			break;
		default:
			break;
		}

	flip();
	bool playing = playmode == PLAYING;
	int clockglow = maxi(96, 255 - calcseqsubstep(0, 256 - 96));
	int flickery = triangle(millis() / 2);
	int flickeryfast = triangle(millis() * 8);
	int loopbright = 96;
	int phase0 = calcseqsubstep(0, 8);

	int cvpitch = adc_get_smooth(ADC_S_PITCH);

	for (int fi = 0; fi < 8; ++fi) {
		Touch* synthf = get_string_touch(fi);
		FingerRecord* fr = readpattern(fi);

		///////////////////////////////////// ROOT NOTE DISPLAY
		/// per finger
		int root = param_eval_finger(P_ROTATE, fi, synthf);
		u32 scale = param_eval_finger(P_SCALE, fi, synthf);
		if (scale >= S_LAST)
			scale = 0;
		int cvquant = param_eval_int(P_CV_QUANT, any_rnd, env16, pressure16);
		if (cvquant == CVQ_SCALE) {
			// remap the 12 semitones input to the scale steps, evenly, with a slight offset so white keys map to major
			// scale etc
			int steps = ((cvpitch / 512) * scale_table[scale][0] + 1) / 12;
			root += steps;
		}
		root += scale_steps_at_string(scale, fi, synthf);

		///////////////////////////////////////////////////////
		int sp0 = get_sample_info()->splitpoints[fi];
		int sp1 = (fi < 7) ? get_sample_info()->splitpoints[fi + 1] : get_sample_info()->samplelen;

		for (int y = 0; y < 8; ++y) {

			int step = fi + y * 8;
			int rotstep = fi * 8 + y;
			int k = 0; // f->pres - (y^7) * 128;
			k = clampi((int)((next[step]) * 64.f) - 20, 0, 128);

			if (synthf->pos / 256 == y)
				k = maxi(k, synthf->pres / 8);

			if (fr && fr->pos[phase0 / 2] / 32 == y)
				k = maxi(k, fr->pres[phase0]);

			bool inloop = ((step - loopstart_step) & 63) < rampreset.looplen_step;
#ifdef NEW_LAYOUT
			int pA = (fi > 0 && fi < 7) ? (fi - 1) + (y) * 12 : P_LAST;
#else
			int p = (fi - 1) + (y - 1) * 12;
#endif

			int pAorB = pA + ((ui_mode == UI_EDITING_B) ? 6 : 0);

			switch (ui_mode) {
			case UI_EDITING_A:
			case UI_EDITING_B: {
				if (fi == 7) {
					if (y == ui_edit_mod)
						k = flickery;
					else {
						// light right side for mod sources that are non zero
						k = (y && GetParam(ui_edit_param, y)) ? 255 : 0;
					}
				}
				else if (fi == 0 && ui_edit_param < P_LAST) {
					// bar graph
bargraph:
					k = 0;
					int v = GetParam(ui_edit_param, ui_edit_mod);
					bool issigned = param_flags[ui_edit_param] & FLAG_SIGNED;
					issigned |= (selected_mod_src != M_BASE);
					int kontrast = 16;

					if (issigned) {
						if (y < 4) {
							k = ((v - (3 - y) * (FULL / 4)) * (192 * 4)) / FULL;
							k = (y) * 2 * kontrast + clampi(k, 0, 191);
							if (y == 3 && v < 0)
								k = 255;
						}
						else {
							k = ((-v - (y - 4) * (FULL / 4)) * (192 * 4)) / FULL;
							k = (8 - y) * 2 * kontrast + clampi(k, 0, 191);
							if (y == 4 && v > 0)
								k = 255;
						}
					}
					else {
						k = ((v - (7 - y) * (FULL / 8)) * (192 * 8)) / FULL;
						k = (y)*kontrast + clampi(k, 0, 191);
					}
				}
				if (fi > 0 && fi < 7) {
					k = 0;
					if ((strip_holds_valid_action & 128) && (strip_is_action_pressed & 128) && ui_edit_mod > 0) {
						// finger down over right side! show all params with a non zero mod amount
						if (GetParam(pAorB, ui_edit_mod))
							k = 255;
					}
					if (pAorB == ui_edit_param)
						k = flickery;
				}
#ifdef NEW_LAYOUT
				if (pAorB == P_ARPONOFF)
					k = (rampreset.flags & FLAGS_ARP) ? 255 : 0;
				else if (pAorB == P_LATCHONOFF)
					k = (rampreset.flags & FLAGS_LATCH) ? 255 : 0;
#else
				if (y == 0) {
					if (fi == 1)
						k = maxi(k, (rampreset.arpon) ? 255 : 0);
					else if (fi == 6)
						k = maxi(k, (sysparams.systemflags & SYS_LATCHON) ? 255 : 0);
				}
#endif
				break;
			}
			case UI_DEFAULT:
				if (fi == 0 && ui_edit_param < P_LAST)
					goto bargraph;
				if (ui_edit_param < P_LAST && (ui_edit_param == pA || ui_edit_param == pA + 6) && fi > 0 && fi < 7)
					k = flickery;
				{
					if (using_sampler() && !get_sample_info()->pitched) {
						int samp = sp0 + (((sp1 - sp0) * y) >> 3);
						const static int zoom = 3;
						u16 avgpeak = getwaveform4zoom(get_sample_info(), samp / 1024, zoom) & 15;
						k = maxi(k, avgpeak * (96 / 16));
					}
					else {
						int pitch = (pitch_at_step(scale, (7 - y) + root));
						pitch %= 12 * 512;
						if (pitch < 0)
							pitch += 12 * 512;
						if (pitch < 256)
							k = maxi(k, 96);
					}
				}

				loopbright = 48;
				// fall thru
			case UI_PTN_START:
			case UI_PTN_END:
				// show looping region faintly; show current playpos brightly
				if (inloop)
					k = maxi(k, loopbright);
				if (step == loopstart_step && ui_mode == UI_PTN_START)
					k = 255;
				if (((step + 1) & 63) == ((loopstart_step + rampreset.looplen_step) & 63) && ui_mode == UI_PTN_END)
					k = 255;
				// playhead
				if (step == cur_step)
					k = maxi(k, clockglow);
				if (step == pending_loopstart_step && playing)
					k = maxi(k, (clockglow * 4) & 255);
				break;
			case UI_LOAD:
				k = (fi >= 4 && fi < 7) ? 64 : 0;
				if (rotstep == pending_preset)
					k = flickeryfast;
				if (rotstep == sysparams.curpreset)
					k = 255;
				if (rotstep == pending_pattern + 32)
					k = flickeryfast;
				if (rotstep == cur_pattern + 32)
					k = 255;
				if (pending_sample1 && rotstep == (pending_sample1 - 1) + 32 + 24)
					k = flickeryfast;
				if (cur_sample_id1 && rotstep == (cur_sample_id1 - 1) + 32 + 24)
					k = 255;
				break;
			default: // suppres warning
				break;
			}

			if (ui_mode == UI_DEFAULT) {
				int delay = 1 + (((7 - y) * (7 - y) + fi * fi) >> 2);
				u8 histpos = (audiohistpos + 31 - delay) & 31;
				u8 ainlvl = audiopeakhistory[histpos];
				u8 ainlvlprev = audiopeakhistory[(histpos - 1) & 31];
				ainlvl = maxi(0, ainlvl - ainlvlprev);                   // highpass
				ainlvl = clampi((ainlvl * (32 - delay)) >> (6), 0, 255); // fade out
				k = maxi(k, ainlvl);
			}
			leds[fi][y] = led_add_gamma(k);
		}
	}
	{
		leds[8][SS_PLAY] = playing ? led_add_gamma(clockglow) : 0;
		leds[8][SS_LEFT] = (ui_mode == UI_PTN_START) ? 255 : 0;
		leds[8][SS_RIGHT] = (ui_mode == UI_PTN_END) ? 255 : 0;
		leds[8][SS_RECORD] = recording ? 255 : 0;
		leds[8][SS_CLEAR] = 0;
		leds[8][SS_LOAD] = (ui_mode == UI_LOAD) ? 255 : 0;
		;
		leds[8][SS_SHIFT_A] = (ui_mode == UI_EDITING_A)                                                       ? 255
		                      : (ui_mode == UI_DEFAULT && ui_edit_param < P_LAST && (ui_edit_param % 12) < 6) ? flickery
		                                                                                                      : 0;
		leds[8][SS_SHIFT_B] = (ui_mode == UI_EDITING_B) ? 255
		                      : (ui_mode == UI_DEFAULT && ui_edit_param < P_LAST && (ui_edit_param % 12) >= 6)
		                          ? flickery
		                          : 0;
		if (shift_state >= 0 && shift_state < 8)
			leds[8][shift_state] = maxi(leds[8][shift_state], 128);
	}
}

void plinky_frame(void) {
	codec_setheadphonevol(sysparams.headphonevol + 45);

	PumpWebUSB(false);

	if (0)
		if ((frame & 31) == 0) {
			DebugLog("spi %d - ", spiduration);
			spiduration = 0;
			tc_log(&_tc_budget, "budget");
			tc_log(&_tc_all, "all");
			DebugLog("\r\n");
		}

	audiopeakhistory[audiohistpos] = clampi(audioin_peak / 64, 0, 255);

	if (g_disable_fx) {
		// web usb is up to its tricks :)
		void draw_webusb_ui(int);
		draw_webusb_ui(0);
		HAL_Delay(30);
		return;
	}

	if (ui_mode == UI_SAMPLE_EDIT) {
		samplemode_ui();
	}
	else {
		edit_mode_ui();
	}
	// rj: this used to be called at every oled_flip(), putting it back here
	update_accelerometer_raw();
	audiohistpos = (audiohistpos + 1) & 31;
	PumpFlashWrites();
}
