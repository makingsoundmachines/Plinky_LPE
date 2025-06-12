#include "gfx/data/names.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/encoder.h"
#include "hardware/flash.h"
#include "hardware/ram.h"
#include "hardware/spi.h"
#include "synth/arp.h"
#include "synth/lfos.h"
#include "synth/params.h"
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
	SampleInfo* s = &cur_sample_info;

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
	SampleInfo* s = &cur_sample_info;
	if (sampler_mode == SM_ERASING) {
		erasepos = 0;
		draw_recording_ui();
		while (spi_state)
			;
		spi_state = 255;
		HAL_Delay(10);
		// mysteriously, sometimes page 0 wasn't erasing. maybe do it twice? WOOAHAHA
		spi_erase64k(0 + record_flashaddr_base, draw_recording_ui);
		for (int addr = 0; addr < MAX_SAMPLE_LEN * 2; addr += 65536) {
			spi_erase64k(addr + record_flashaddr_base, draw_recording_ui);
			erasepos++;
		}
		memset(s, 0, sizeof(SampleInfo)); // since we erased the ram, we might as well nuke the sample too
		log_ram_edit(SEG_SAMPLE);
		// done!
		spi_state = 0;
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
		while (spi_state)
			;
		spi_state = 0xff; // prevent spi reads for a while!
		while (buf_read_pos + 256 / 2 <= locrecpos && s->samplelen < MAX_SAMPLE_LEN) {
			s16* src = delaybuf + (buf_read_pos & DLMASK);
			s16* dst = (s16*)(spi_bit_tx + 4);
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
			log_ram_edit(SEG_SAMPLE);
			// sample full => stop recording
			if (s->samplelen >= MAX_SAMPLE_LEN) {
				stop_recording_sample();
				break;
			}
		} // spi write loop

		spi_state = 0;
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
	vr += OLED_WIDTH - 16;
	u8 draw_frame = (lfo_scope_frame + 1) & 15;
	for (u8 x = 0; x < 16; ++x) {
		vr[0] &= ~(lfo_scope_data[draw_frame][0] >> 1);
		vr[128] &= ~(lfo_scope_data[draw_frame][1] >> 1);
		vr[256] &= ~(lfo_scope_data[draw_frame][2] >> 1);
		vr[384] &= ~(lfo_scope_data[draw_frame][3] >> 1);

		vr[0] |= lfo_scope_data[draw_frame][0];
		vr[128] |= lfo_scope_data[draw_frame][1];
		vr[256] |= lfo_scope_data[draw_frame][2];
		vr[384] |= lfo_scope_data[draw_frame][3];
		vr++;
		draw_frame = (draw_frame + 1) & 15;
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
	u8 rightOffset = latch_on() ? 38 : 14;
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
	if (arp_on()) {
		fill_rectangle(128 - 32, 0, 128 - 17, 8);
		draw_str(-(128 - 17), -1, F_8, "arp");
	}
	if (latch_on()) {
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
	int valmax = param_range[p] & RANGE_MASK;
	int vscale = valmax ? (mini(v, PARAM_SIZE - 1) * valmax) / PARAM_SIZE : v;
	int displaymax = valmax ? valmax * 10 : 1000;
	bool decimal = true;
	//	const char* val = valbuf;
	if (mod == SRC_BASE)
		switch (p) {
		case P_SMP_STRETCH:
		case P_SMP_SPEED:
			displaymax = 2000;
			break;
		case P_ARP_TOGGLE:
			if (mod)
				return "";
			return (arp_on()) ? "On" : "Off";
		case P_LATCH_TOGGLE:
			if (mod)
				return "";
			return latch_on() ? "On" : "Off";
		case P_SAMPLE:
			if (vscale == 0) {
				return "Off";
			}
			break;
		case P_SEQ_CLK_DIV: {
			if (vscale >= NUM_SYNC_DIVS)
				return "(Gate CV)";
			int n = sprintf(valbuf, "%d", sync_divs_32nds[vscale] /* >> divisor*/);
			if (!decbuf)
				decbuf = valbuf + n;
			sprintf(decbuf, /*divisornames[divisor]*/ "/32");
			return valbuf;
		}
		case P_ARP_CLK_DIV:
			if (v < 0) {
				v = -v;
				decimal = true;
				valmax = 0;
				displaymax = 1000;
				break;
			}
			vscale = (mini(v, PARAM_SIZE - 1) * NUM_SYNC_DIVS) / PARAM_SIZE;
			int n = sprintf(valbuf, "%d", sync_divs_32nds[vscale] /*>> divisor*/);
			if (!decbuf)
				decbuf = valbuf + n;
			sprintf(decbuf, /*divisornames[divisor]*/ "/32");
			return valbuf;
		case P_ARP_OCTAVES:
			v += (PARAM_SIZE * 10) / displaymax; // 1 based
			break;
		case P_MIDI_CH_IN:
		case P_MIDI_CH_OUT: {
			int midich = clampi(vscale, 0, 15) + 1;
			int n = sprintf(valbuf, "%d", midich);
			if (!decbuf)
				decbuf = valbuf + n;
			return valbuf;
		}
		case P_ARP_ORDER:
			return arp_modenames[clampi(vscale, 0, NUM_ARP_ORDERS - 1)];
		case P_SEQ_ORDER:
			return seqmodenames[clampi(vscale, 0, NUM_SEQ_ORDERS - 1)];
		case P_CV_QUANT:
			return cvquantnames[clampi(vscale, 0, CVQ_LAST - 1)];
		case P_SCALE:
			return scalenames[clampi(vscale, 0, NUM_SCALES - 1)];
		case P_A_SHAPE:
		case P_B_SHAPE:
		case P_X_SHAPE:
		case P_Y_SHAPE:
			return lfo_names[clampi(vscale, 0, NUM_LFO_SHAPES - 1)];
		case P_PITCH:
		case P_INTERVAL:
			displaymax = 120;
			break;
		case P_TEMPO:
			v += PARAM_SIZE;
			if (!using_internal_clock)
				v = (bpm_10x * PARAM_SIZE) / 1200;
			displaymax = 1200;
			break;
		case P_DLY_TIME:
			if (v < 0) {
				if (v <= -1024)
					v++;
				v = (-v * 13) / PARAM_SIZE;
				int n = sprintf(valbuf, "%d", sync_divs_32nds[v]);
				if (!decbuf)
					decbuf = valbuf + n;
				sprintf(decbuf, "/32 sync");
			}
			else {
				int n = sprintf(valbuf, "%d", (v * 100) / PARAM_SIZE);
				if (!decbuf)
					decbuf = valbuf + n;
				sprintf(decbuf, "free");
			}
			return valbuf;
		default:;
		}
	v = (v * displaymax) / PARAM_SIZE;
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
				if (arp_on() && !arp_touched(x))
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
	memcpy(presetname, cur_preset.name, 8);
	presetname[8] = 0;

	u8* vr = oled_buffer();
	static u8 ui_edit_param = NUM_PARAMS;
	u8 ep = selected_param;
	if (ui_edit_param != ep) {
		ui_edit_param = ep; // as it may change in the background
	}
	u8 ui_edit_mod = selected_mod_src;
	u8 start_step = cur_seq_start;

	const char* pagename = 0;
	if (shift_state == SS_RECORD) {
		if (shift_short_pressed())
			draw_str(0, 4, F_20_BOLD, seq_recording() ? I_RECORD "record >off" : I_RECORD "record >on");
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
				DrawSamplePlayback(&cur_sample_info);
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
			if (ui_edit_param < NUM_PARAMS)
				goto draw_parameter;

			if (mem_param < NUM_PARAMS && enc_recently_used())
				goto draw_parameter;
			DrawFlags();
			char seqicon = arp_on() ? I_NOTES[0] : I_SEQ[0];
			char preseticon = I_PRESET[0];
			int xtab = 0;
			if (cued_preset_id != 255 && cued_preset_id != sys_params.curpreset)
				xtab = fdraw_str(0, 0, F_20_BOLD, "%c%d->%d", preseticon, sys_params.curpreset + 1, cued_preset_id + 1);
			else if (synth_max_pres > 1 && !(using_sampler() && !cur_sample_info.pitched)) {
				xtab = fdraw_str(0, 0, F_20_BOLD, "%s", notename((high_string_pitch + 1024) / 2048));
			}
			else
				xtab = fdraw_str(0, 0, F_20_BOLD, "%c%d", preseticon, sys_params.curpreset + 1);
			draw_str(xtab + 2, 0, F_8_BOLD, presetname);
			if (cur_preset.category > 0 && cur_preset.category < CAT_LAST)
				draw_str(xtab + 2, 8, F_8, kpresetcats[cur_preset.category]);
			if (cued_pattern_id != 255 && cued_pattern_id != cur_pattern_id)
				fdraw_str(0, 16, F_20_BOLD, "%c%d->%d", seqicon, cur_pattern_id + 1, cued_pattern_id + 1);
			else
				fdraw_str(0, 16, F_20_BOLD, "%c%d", seqicon, cur_pattern_id + 1);
			break;
		case UI_EDITING_A:
		case UI_EDITING_B:
			if (ui_edit_param >= NUM_PARAMS) {
				draw_str(0, 0, F_20_BOLD, modnames[ui_edit_mod]);
				draw_str(0, 16, F_16, "select parameter");
				break;
			}
			else {
				DrawLFOs();
				int pi;
draw_parameter:
				pi = ui_edit_param;
				if (pi >= NUM_PARAMS)
					pi = mem_param;
				if (pi >= NUM_PARAMS)
					pi = 0;
				pagename = param_page_names[pi / 6];
				switch (pi) {
				case P_TEMPO:
					pagename = I_TEMPO "Tap";
					break;

				case P_NOISE:
					pagename = I_WAVE "noise";
					break;
				case P_CV_QUANT:
				case P_VOLUME:
					pagename = "system";
					break;
				}
				draw_str(0, 0, F_12, (ui_edit_mod == 0) ? pagename : modnames[ui_edit_mod]);
				const char* pn = param_names[pi];
				int pw = str_width(F_16_BOLD, pn);
				if (pw > 64)
					draw_str(0, 20, F_12_BOLD, pn);
				else
					draw_str(0, 16, F_16_BOLD, pn);
				char valbuf[32];
				char decbuf[16];
				int w = 0;
				int v = param_val_raw(pi, ui_edit_mod);
				int vbase = v;
				if (ui_edit_mod == SRC_BASE) {
					v = (param_val_unscaled(pi) * PARAM_SIZE) >> 16;
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
			fdraw_str(0, 0, F_20_BOLD, I_PREV "Start %d", start_step + 1);
			fdraw_str(0, 16, F_20_BOLD, I_PLAY "Current %d", cur_seq_step + 1);
			break;
		case UI_PTN_END:
			fdraw_str(0, 0, F_20_BOLD, I_NEXT "End %d", ((cur_preset.seq_len + start_step) & 63) + 1);
			fdraw_str(0, 16, F_20_BOLD, I_INTERVAL "Length %d", cur_preset.seq_len);
			break;
		case UI_LOAD:
			if (shift_state_frames > 4 && shift_state == SS_CLEAR) {
				bool done = (shift_state_frames - 4) > 64;
				if (recent_load_item < PATTERNS_START)
					fdraw_str(0, 0, F_16_BOLD,
					          done ? "cleared\n" I_PRESET "Preset %d" : "initialize\n" I_PRESET "Preset %d?",
					          recent_load_item + 1);
				else if (recent_load_item < SAMPLES_START)
					fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_SEQ "Pattern %d." : "Clear\n" I_SEQ "Pattern %d?",
					          recent_load_item - PATTERNS_START + 1);
				else
					fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_WAVE "Sample %d." : "Clear\n" I_WAVE "Sample %d?",
					          recent_load_item - SAMPLES_START + 1);
				inverted_rectangle(0, 0, shift_state_frames * 2 - 4, 32);
			}
			else if (long_press_frames >= 32) {
				bool done = (long_press_frames - 32) > 128;
				if (long_press_pad >= 56)
					fdraw_str(0, 0, F_16_BOLD, done ? "ok!" : "Edit\n" I_WAVE "Sample %d?", long_press_pad - 56 + 1);
				else if (long_press_pad == copy_preset_id)
					fdraw_str(0, 0, F_16_BOLD,
					          done ? "toggled\n" I_PRESET "Preset %d" : "toggle\n" I_PRESET "Preset %d?",
					          copy_preset_id + 1);
				else if (long_press_pad < 32)
					fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_PRESET "%d" : "copy over\n" I_PRESET "Preset %d?",
					          long_press_pad + 1);
				else
					fdraw_str(0, 0, F_16_BOLD, done ? "copied to " I_SEQ "%d" : "copy over\n" I_SEQ "Pat %d?",
					          long_press_pad - 32 + 1);

				inverted_rectangle(0, 0, long_press_frames - 32, 32);
			}
			else {
				int xtab = fdraw_str(0, 0, F_20_BOLD, I_PRESET "%d", sys_params.curpreset + 1);
				draw_str(xtab + 2, 0, F_8_BOLD, presetname);
				if (cur_preset.category > 0 && cur_preset.category < CAT_LAST)
					draw_str(xtab + 2, 8, F_8, kpresetcats[cur_preset.category]);

				fdraw_str(0, 16, F_20_BOLD, I_SEQ "Pat %d ", cur_pattern_id + 1);
				fdraw_str(-128, 16, F_20_BOLD, cur_sample_id < NUM_SAMPLES ? I_WAVE "%d" : I_WAVE "Off",
				          cur_sample_id + 1);
			}
			break;
		default:
			break;
		}

	flip();
	int clockglow = maxi(96, 255 - seq_substep(256 - 96));
	int flickery = triangle(millis() / 2);
	int flickeryfast = triangle(millis() * 8);
	int loopbright = 96;
	int phase0 = seq_substep(8);

	int cvpitch = adc_get_smooth(ADC_S_PITCH);

	for (int fi = 0; fi < 8; ++fi) {
		PatternStringStep* fr = string_step_ptr(fi, true);

		///////////////////////////////////// ROOT NOTE DISPLAY
		/// per finger
		int root = param_val_poly(P_DEGREE, fi);
		u32 scale = param_val_poly(P_SCALE, fi);
		if (scale >= NUM_SCALES)
			scale = 0;
		int cvquant = param_val(P_CV_QUANT);
		if (cvquant == CVQ_SCALE) {
			// remap the 12 semitones input to the scale steps, evenly, with a slight offset so white keys map to major
			// scale etc
			int steps = ((cvpitch / 512) * scale_table[scale][0] + 1) / 12;
			root += steps;
		}
		root += scale_steps_at_string(scale, fi);
		Touch* synthf = get_string_touch(fi);
		///////////////////////////////////////////////////////
		int sp0 = cur_sample_info.splitpoints[fi];
		int sp1 = (fi < 7) ? cur_sample_info.splitpoints[fi + 1] : cur_sample_info.samplelen;

		for (int y = 0; y < 8; ++y) {

			int step = fi + y * 8;
			int rotstep = fi * 8 + y;
			int k = 0; // f->pres - (y^7) * 128;
			k = clampi((int)((next[step]) * 64.f) - 20, 0, 128);

			if (synthf->pos / 256 == y)
				k = maxi(k, synthf->pres / 8);

			if (fr && fr->pos[phase0 / 2] / 32 == y)
				k = maxi(k, fr->pres[phase0]);

			bool inloop = ((step - start_step) & 63) < cur_preset.seq_len;
#ifdef NEW_LAYOUT
			int pA = (fi > 0 && fi < 7) ? (fi - 1) + (y) * 12 : NUM_PARAMS;
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
						k = (y && param_val_raw(ui_edit_param, y)) ? 255 : 0;
					}
				}
				else if (fi == 0 && ui_edit_param < NUM_PARAMS) {
					// bar graph
bargraph:
					k = 0;
					int v = param_val_raw(ui_edit_param, ui_edit_mod);
					bool issigned = param_range[ui_edit_param] & RANGE_SIGNED;
					issigned |= (selected_mod_src != SRC_BASE);
					int kontrast = 16;

					if (issigned) {
						if (y < 4) {
							k = ((v - (3 - y) * (PARAM_SIZE / 4)) * (192 * 4)) / PARAM_SIZE;
							k = (y) * 2 * kontrast + clampi(k, 0, 191);
							if (y == 3 && v < 0)
								k = 255;
						}
						else {
							k = ((-v - (y - 4) * (PARAM_SIZE / 4)) * (192 * 4)) / PARAM_SIZE;
							k = (8 - y) * 2 * kontrast + clampi(k, 0, 191);
							if (y == 4 && v > 0)
								k = 255;
						}
					}
					else {
						k = ((v - (7 - y) * (PARAM_SIZE / 8)) * (192 * 8)) / PARAM_SIZE;
						k = (y)*kontrast + clampi(k, 0, 191);
					}
				}
				if (fi > 0 && fi < 7) {
					k = 0;
					if ((strip_holds_valid_action & 128) && (strip_is_action_pressed & 128) && ui_edit_mod > 0) {
						// finger down over right side! show all params with a non zero mod amount
						if (param_val_raw(pAorB, ui_edit_mod))
							k = 255;
					}
					if (pAorB == ui_edit_param)
						k = flickery;
				}
#ifdef NEW_LAYOUT
				if (pAorB == P_ARP_TOGGLE)
					k = arp_on() ? 255 : 0;
				else if (pAorB == P_LATCH_TOGGLE)
					k = latch_on() ? 255 : 0;
#else
				if (y == 0) {
					if (fi == 1)
						k = maxi(k, (cur_preset.arpon) ? 255 : 0);
					else if (fi == 6)
						k = maxi(k, (sys_params.systemflags & SYS_LATCHON) ? 255 : 0);
				}
#endif
				break;
			}
			case UI_DEFAULT:
				if (fi == 0 && ui_edit_param < NUM_PARAMS)
					goto bargraph;
				if (ui_edit_param < NUM_PARAMS && (ui_edit_param == pA || ui_edit_param == pA + 6) && fi > 0 && fi < 7)
					k = flickery;
				{
					if (using_sampler() && !cur_sample_info.pitched) {
						int samp = sp0 + (((sp1 - sp0) * y) >> 3);
						const static int zoom = 3;
						u16 avgpeak = getwaveform4zoom(&cur_sample_info, samp / 1024, zoom) & 15;
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
				if (step == start_step && ui_mode == UI_PTN_START)
					k = 255;
				if (((step + 1) & 63) == ((start_step + cur_preset.seq_len) & 63) && ui_mode == UI_PTN_END)
					k = 255;
				// playhead
				if (step == cur_seq_step)
					k = maxi(k, clockglow);
				if (step == cued_ptn_start && seq_playing())
					k = maxi(k, (clockglow * 4) & 255);
				break;
			case UI_LOAD:
				k = (fi >= 4 && fi < 7) ? 64 : 0;
				if (rotstep == cued_preset_id)
					k = flickeryfast;
				if (rotstep == sys_params.curpreset)
					k = 255;
				if (rotstep == PATTERNS_START + cued_pattern_id)
					k = flickeryfast;
				if (rotstep == PATTERNS_START + cur_pattern_id)
					k = 255;
				if (cued_sample_id && rotstep == SAMPLES_START + cued_sample_id)
					k = flickeryfast;
				if (cur_sample_id < NUM_SAMPLES && rotstep == SAMPLES_START + cur_sample_id)
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
		leds[8][SS_PLAY] = seq_playing() ? led_add_gamma(clockglow) : 0;
		leds[8][SS_LEFT] = (ui_mode == UI_PTN_START) ? 255 : 0;
		leds[8][SS_RIGHT] = (ui_mode == UI_PTN_END) ? 255 : 0;
		leds[8][SS_RECORD] = seq_recording() ? 255 : 0;
		leds[8][SS_CLEAR] = 0;
		leds[8][SS_LOAD] = (ui_mode == UI_LOAD) ? 255 : 0;
		;
		leds[8][SS_SHIFT_A] = (ui_mode == UI_EDITING_A) ? 255
		                      : (ui_mode == UI_DEFAULT && ui_edit_param < NUM_PARAMS && (ui_edit_param % 12) < 6)
		                          ? flickery
		                          : 0;
		leds[8][SS_SHIFT_B] = (ui_mode == UI_EDITING_B) ? 255
		                      : (ui_mode == UI_DEFAULT && ui_edit_param < NUM_PARAMS && (ui_edit_param % 12) >= 6)
		                          ? flickery
		                          : 0;
		if (shift_state >= 0 && shift_state < 8)
			leds[8][shift_state] = maxi(leds[8][shift_state], 128);
	}
}

void plinky_frame(void) {
	codec_setheadphonevol(sys_params.headphonevol + 45);

	PumpWebUSB(false);

	audiopeakhistory[audiohistpos] = clampi(audioin_peak / 64, 0, 255);

	// if (g_disable_fx) {
	// 	// web usb is up to its tricks :)
	// 	void draw_webusb_ui(int);
	// 	draw_webusb_ui(0);
	// 	HAL_Delay(30);
	// 	return;
	// }

	if (ui_mode == UI_SAMPLE_EDIT) {
		samplemode_ui();
	}
	else {
		edit_mode_ui();
	}
	// reading the accelerometer needs to live in the main thread because it involves (blocking) I2C communication
	accel_read();
	audiohistpos = (audiohistpos + 1) & 31;

	// always do a ram_frame, except when in ui_sample_edit *and* working on a new sample
	if (sampler_mode == SM_PREVIEW)
		ram_frame();
}
