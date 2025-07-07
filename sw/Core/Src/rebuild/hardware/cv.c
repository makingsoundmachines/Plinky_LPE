#include "cv.h"
#include "adc_dac.h"
#include "gfx/gfx.h"
#include "leds.h"
#include "touchstrips.h"

// cleanup
extern s8 enable_audio;

// this is defined in main.c
extern DAC_HandleTypeDef hdac1;

void send_cv_pitch_hi(s32 data, bool apply_calib) {
	if (apply_calib) {
		data = (s32)((data * adc_dac_calib[DAC_PITCH_CV_HI].scale) + adc_dac_calib[DAC_PITCH_CV_HI].bias);
		s32 step = abs((s32)(adc_dac_calib[DAC_PITCH_CV_HI].scale * (2048.f * 12.f)));
		for (u8 k = 0; k < 3; ++k)
			if (data > 65535)
				data -= step;
			else
				break;
		for (u8 k = 0; k < 3; ++k)
			if (data < 0)
				data += step;
			else
				break;
	}
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_L, clampi(data, 0, 65535));
}

void send_cv_pitch_lo(s32 data, bool apply_calib) {
	if (apply_calib) {
		data = (s32)((data * adc_dac_calib[DAC_PITCH_CV_LO].scale) + adc_dac_calib[DAC_PITCH_CV_LO].bias);
		s32 step = abs((int)(adc_dac_calib[DAC_PITCH_CV_LO].scale * (2048.f * 12.f)));
		for (s32 k = 0; k < 3; ++k)
			if (data > 65535)
				data -= step;
			else
				break;
		for (s32 k = 0; k < 3; ++k)
			if (data < 0)
				data += step;
			else
				break;
	}
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_L, clampi(data, 0, 65535));
}

// cv calib
void cv_calib(void) {
	enable_audio = EA_OFF;
	oled_clear();
	int topscroll = 128;
	const char* topline = "unplug all inputs. use left 4 columns to adjust pitch cv outputs. plug pitch lo output to "
	                      "pitch input when done.";
	int toplinew = str_width(F_16, topline);
	const char* const botlines[5] = {"plug gate out->in", "Pitch lo=0v/C0", "Pitch lo=2v/C2", "Pitch hi=0v/C0",
	                                 "Pitch hi=2v/C2"};
	u8 ff = touch_frame;
	float adcavgs[6][2];
	for (int i = 0; i < 6; ++i)
		adcavgs[i][0] = -1.f;
	int curx = -1;
	float downpos[4] = {}, downval[4] = {};
	float cvout[4] = {
	    adc_dac_calib[DAC_PITCH_CV_LO].bias,
	    adc_dac_calib[DAC_PITCH_CV_LO].bias + adc_dac_calib[DAC_PITCH_CV_LO].scale * 2048.f * 24.f,
	    adc_dac_calib[DAC_PITCH_CV_HI].bias,
	    adc_dac_calib[DAC_PITCH_CV_HI].bias + adc_dac_calib[DAC_PITCH_CV_HI].scale * 2048.f * 24.f,
	};
	send_cv_pitch_lo((int)cvout[0], false);
	send_cv_pitch_hi((int)cvout[2], false);
	u8 curlo = 0;
	u8 curhi = 2;
	bool prevprevpitchsense = true;
	bool prevpitchsense = true;
	while (1) {
		oled_clear();
		drawstr_noright(topscroll, 0, F_16, topline);
		bool gateok = cv_gate_present();
		gateok = !gateok; // in new layout, theres a bleed resistor so no need for gate - in fact, we dont want it
		draw_str(0, 18, F_12_BOLD, (curx < 0 && gateok) ? "pitch out>in when done" : botlines[curx + 1]);
		if (curx >= 0)
			fdraw_str(-128, 24, F_8, "(%d)", (int)cvout[curx]);
		oled_flip();
		topscroll -= 2;
		if (topscroll < -toplinew)
			topscroll = 128;
		while (touch_frame == ff)
			; // wait for new touch data
		ff = touch_frame;
		int pitchsense = cv_pitch_present();
		if (pitchsense && !prevprevpitchsense && gateok)
			break;
		prevprevpitchsense = prevpitchsense;
		prevpitchsense = pitchsense;

		// calibrate the 0 point for the inputs
		for (int i = 0; i < 6; ++i) {
			int tot = 0;
			for (int j = 0; j < ADC_SAMPLES; ++j)
				tot += adc_buffer[j * ADC_CHANS + i];
			tot /= ADC_SAMPLES;
			if (adcavgs[i][0] < 0)
				adcavgs[i][0] = adcavgs[i][1] = tot;
			adcavgs[i][0] += (tot - adcavgs[i][0]) * 0.05f;
			adcavgs[i][1] += (adcavgs[i][0] - adcavgs[i][1]) * 0.05f;
		}
		for (int x = 0; x < 4; ++x) {
			Touch* f = get_touch_prev(x, 1);
			Touch* pf = get_touch_prev(x, 2);
			if (f->pres >= 500) {
				if (pf->pres < 500) {
					downpos[x] = f->pos;
					downval[x] = cvout[x];
					curx = x;
				}
				float delta = deadzone(f->pos - downpos[x], 64.f);
				//				delta=(delta*delta)>>12;
				cvout[x] += (clampf(downval[x] + delta * 0.25f, 0.f, 65535.f) - cvout[x]) * 0.1f;
				if (x < 2)
					curlo = x;
				else
					curhi = x;
			}
		}
		send_cv_pitch_lo((int)cvout[curlo], false);
		send_cv_pitch_hi((int)cvout[curhi], false);

		// update the leds, innit
		for (int fi = 0; fi < 9; ++fi) {
			for (int y = 0; y < 8; ++y) {
				int k = (fi < 4) ? (triangle(y * 64 - (int)cvout[fi] / 4) / 4) : 128;
				leds[fi][y] = led_add_gamma(((fi == curx) ? 255 : 128) - k);
			}
		}
	}
	// zero is now nicely set
	for (int i = 0; i < 6; ++i) {
		adc_dac_calib[i].bias = adcavgs[i][1];
		adc_dac_calib[i].scale = 0.2f / -6548.1f;
		DebugLog("adc zero point %d - %d\r\n", i, (int)adcavgs[i][1]);
	}
	adc_dac_calib[ADC_B_KNOB].bias = 32000.f; // hw seems to skew towards 0 slightly...
	adc_dac_calib[ADC_B_KNOB].scale = 1.05f / -32768.f;
	adc_dac_calib[ADC_A_KNOB].bias = 32000.f;
	adc_dac_calib[ADC_A_KNOB].scale = 1.05f / -32768.f;

	// output calib is now nicely set
	adc_dac_calib[DAC_PITCH_CV_LO].bias = cvout[0];
	adc_dac_calib[DAC_PITCH_CV_LO].scale = (cvout[1] - cvout[0]) * 1.f / (2048.f * 24.f);
	adc_dac_calib[DAC_PITCH_CV_HI].bias = cvout[2];
	adc_dac_calib[DAC_PITCH_CV_HI].scale = (cvout[3] - cvout[2]) * 1.f / (2048.f * 24.f);
	for (int i = 0; i < 4; ++i)
		DebugLog("selected dac value %d - %d\r\n", i, (int)cvout[i]);
	DebugLog("dac pitch lo zero point %d, step*1000 %d\r\n", (int)adc_dac_calib[DAC_PITCH_CV_LO].bias,
	         (int)(adc_dac_calib[DAC_PITCH_CV_LO].scale * 1000.f));
	DebugLog("dac pitch hi zero point %d, step*1000 %d\r\n", (int)adc_dac_calib[DAC_PITCH_CV_HI].bias,
	         (int)(adc_dac_calib[DAC_PITCH_CV_HI].scale * 1000.f));

	// use it to calibrate

	oled_clear();
	draw_str(0, 4, F_12, "waiting for pitch\nloopback cable");
	oled_flip();
	HAL_Delay(1000);
	// wait for them to plug the other end in
	while (1) {
		int tots[2] = {0};
		for (int hilo = 0; hilo < 2; ++hilo) {
			send_cv_pitch_lo((int)cvout[hilo], false);
			send_cv_pitch_hi((int)cvout[hilo + 2], false);
			HAL_Delay(50);
			int tot = 0;
			for (int j = 0; j < ADC_SAMPLES; ++j)
				tot += adc_buffer[j * ADC_CHANS + ADC_PITCH];
			tot /= ADC_SAMPLES;
			tots[hilo] = tot;
		}
		if (abs(tots[0] - tots[1]) > 5000)
			break;
	}
	oled_clear();
	draw_str(0, 4, F_24_BOLD, "just a mo...");
	oled_flip();
	HAL_Delay(1000);
	for (int hilo = 0; hilo < 2; ++hilo) {
		send_cv_pitch_lo((int)cvout[hilo], false);
		send_cv_pitch_hi((int)cvout[hilo + 2], false);
		HAL_Delay(50);
		int tot = 0;
		for (int iter = 0; iter < 256; ++iter) {
			HAL_Delay(2);
			for (int j = 0; j < ADC_SAMPLES; ++j)
				tot += adc_buffer[j * ADC_CHANS + ADC_PITCH];
		}
		tot /= ADC_SAMPLES * 256;
		DebugLog("pitch adc for hilo=%d is %d\r\n", hilo, tot);
		if (hilo == 0)
			adc_dac_calib[ADC_PITCH].bias = tot;
		else
			adc_dac_calib[ADC_PITCH].scale = 2.f / (minf(-0.00001f, tot - adc_dac_calib[ADC_PITCH].bias));
	}
	oled_clear();
	draw_str(0, 0, F_16_BOLD, "Done!");
	draw_str(0, 16, F_12_BOLD, "Unplug pitch cable!");
	oled_flip();
	while (cv_pitch_present()) {
		HAL_Delay(1);
	}
}