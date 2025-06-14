#include "gfx/gfx.h"
#include "hardware/cv.h"
#include "hardware/encoder.h"
#ifdef HALF_FLASH
const static int calib_sector = -1;
#else
const static int calib_sector = 255;
#endif
const static uint64_t MAGIC = 0xf00dcafe473ff02a; // bump on change
bool flash_writecalib(int which);
int flash_readcalib(void) {
#ifdef EMU
	openflash();
#endif
#if defined EMU // WASM?
#ifndef CALIB_TEST
	if (1) {
		// fake calib results
		for (int i = 0; i < 18; ++i) {
			for (int j = 0; j < 8; ++j)
				calibresults[i].pres[j] = 8000, calibresults[i].pos[j] = (512 * j - 1792) * (((i % 9) == 8) ? -1 : 1);
		}
		return 3;
	}
#endif
#endif
	volatile uint64_t* flash = (volatile uint64_t*)(FLASH_ADDR_256 + calib_sector * 2048);
	int ver = 0, ok = 0;
	if (flash[0] == MAGIC && flash[255] == ~MAGIC)
		ver = 2;
	if (ver == 0) {
		DebugLog("no calibration found in flash\r\n");
		return 0;
	}
	volatile uint64_t* s = flash + 1;
	if (*s != ~(uint64_t)(0)) {
		ok |= 1;
		memcpy(calibresults, (uint64_t*)s, sizeof(calibresults));
	}
	s += sizeof(calibresults) / 8;
	if (*s != ~(uint64_t)(0)) {
		ok |= 2;
		memcpy(adc_dac_calib, (int64_t*)s, sizeof(adc_dac_calib));
	}
	s += sizeof(adc_dac_calib) / 8;
	return ok;
}

bool flash_writecalib(int which) {
	HAL_FLASH_Unlock();
	int rv = flash_erase_page(calib_sector);
	if (rv == 0) {
		uint64_t* flash = (uint64_t*)(FLASH_ADDR_256 + calib_sector * 2048);
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (uint32_t)(size_t)flash, MAGIC);
		uint64_t* d = flash + 1;
		if (which & 1)
			flash_program_block(d, calibresults, sizeof(calibresults));
		d += (sizeof(calibresults) + 7) / 8;
		if (which & 2)
			flash_program_block(d, adc_dac_calib, sizeof(adc_dac_calib));
		d += (sizeof(adc_dac_calib) + 7) / 8;
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (uint32_t)(size_t)(flash + 255), ~MAGIC);
	}
	HAL_FLASH_Lock();
	return 0;
}

extern s8 enable_audio;

void reflash(void);
extern volatile u8 gotclkin;

void led_test(void) {
	enable_audio = EA_PASSTHRU;
	for (int y = 0; y < 9; ++y)
		for (int x = 0; x < 8; ++x)
			leds[y][x] = 255;
	u16 tri = 128;
	int encoder_down_count = -1;
	while (1) {
		oled_clear();
		if (encoder_pressed) {
			if (encoder_down_count >= 0)
				encoder_down_count++;
		}
		else {
			if (encoder_down_count > 2) {
				HAL_Delay(20);
				return;
			}
			encoder_down_count = 0;
		}
		if (encoder_down_count > 100)
			reflash();
		fdraw_str(0, 2, F_12, "TEST %d %d %d %d %02x", adc_buffer[ADC_PITCH] / 256, adc_buffer[ADC_GATE] / 256,
		          adc_buffer[ADC_X_CV] / 256, adc_buffer[ADC_Y_CV] / 256, gotclkin);
		fdraw_str(0, 18, F_12, "%d %d %d %d %d %d", adc_buffer[ADC_A_CV] / 256, adc_buffer[ADC_B_CV] / 256,
		          adc_buffer[ADC_A_KNOB] / 256, adc_buffer[ADC_B_KNOB] / 256, encoder_value >> 2, encoder_pressed);
		oled_flip();
		HAL_Delay(20);
		for (int srcidx = 0; srcidx < NUM_TOUCHES; ++srcidx) {
			int rawpressure = sensor_reading_pressure(srcidx);
			int rawpos = sensor_reading_position(srcidx);
			if (rawpressure > 300) {
				DebugLog("f %d - a=%4d b=%4d amin=%4d bmin=%4d pos=%4d pr=%4d   \r\n", srcidx, rawpos, rawpressure);
			}
		}
		tri += 256;
		send_cv_trigger((tri < 16384) ? true : false);
		send_cv_clock((tri < (16384 + 32768)) ? true : false);
		send_cv_pressure(tri);
		send_cv_gate((tri * 2) & 65535);
		send_cv_pitch_lo(tri, false);
		send_cv_pitch_hi((tri * 2) & 65535, false);
	}
}

void calib(void) {
again:
#ifdef WASM
	return;
#endif

	enable_audio = EA_OFF;
	reset_touches();

	HAL_Delay(20);
	CalibProgress* state = GetCalibProgress(0);
	memset(state, 0, sizeof(CalibProgress) * 18);
	int prevrawpressure[18] = {0};
	s8 curstep[9] = {7, 7, 7, 7, 7, 7, 7, 7, 7};
	int done = false;
	u8 ff = touch_frame;
	u8 refreshscreen = 0;
	char helptext[64] = "slowly/evenly press lit pads.\ntake care, be accurate!";
	bool blink = false;
	int errors = 0;
	while (!done) {

		if (!refreshscreen) {
			refreshscreen = 16;
			blink = !blink;
			oled_clear();
			fdraw_str(0, 0, F_16, "Calibration%c", blink ? '!' : ' ');
			draw_str(0, 16, F_8, helptext);
			if (errors)
				inverted_rectangle(0, 0, 128, 32);
			oled_flip();
		}
		else {
			refreshscreen--;
		}

		if (encoder_pressed) {
			led_test();
			goto again;
		}

		while (touch_frame == ff)
			; // wait for new touch data
		ff = touch_frame;
		// update the 18 calibration entries for the current step
		done = 0;
		int readymask = 0;
		for (int si = 0; si < NUM_TOUCH_READINGS; ++si) {
			int amin = sensor_min[si * 2];
			int bmin = sensor_min[si * 2 + 1];
			int amax = sensor_max[si * 2];
			int bmax = sensor_max[si * 2 + 1];
			int rawpressure = sensor_reading_pressure(si);
			int prevrawp = prevrawpressure[si];
			int rawpos = sensor_reading_position(si);
			int step = curstep[si % NUM_TOUCHES];

			int pressureband = rawpressure / 20;
			if (step >= 0 && step < 8 && rawpressure > 1200 && rawpressure > prevrawp - pressureband / 2
			    && rawpressure < prevrawp + pressureband) {
				// pressure is quite stable
				float w = (rawpressure - 1200.f) / 1000.f;
				float change = abs(prevrawp - rawpressure) * (1.f / 250.f);
				w *= maxf(0.f, 1.f - change);
				if (w > 1.f)
					w = 1.f;
				w *= w;
				const static float LEAK = 0.90f;
				state[si].weight[step] *= LEAK;
				state[si].pos[step] *= LEAK;
				state[si].pres[step] *= LEAK;
				state[si].weight[step] += w;
				state[si].pos[step] += rawpos * w;
				state[si].pres[step] += rawpressure * w;

				if (0)
					if (si < 9)
						DebugLog("finger %d step %d pos %4d %4d pressure %5d %5d weight %3d %d      \r\n", si, step,
						         (int)(state[si].pos[step] / state[si].weight[step]),
						         (int)(state[si + 9].pos[step] / state[si + 9].weight[step]),
						         (int)(state[si].pres[step] / state[si].weight[step]),
						         (int)(state[si + 9].pres[step] / state[si + 9].weight[step]),
						         (int)state[si].weight[step], (int)state[si + 9].weight[step]);
			}
			int ti = si + 9;
			bool ready =
			    si < 9 && step < 8 && step >= 0 && state[si].weight[step] > 4.f && state[ti].weight[step] > 4.f;
			if (ready) {
				if (rawpressure < 900) {
					// move on!
					calibresults[si].pres[step] = state[si].pres[step] / state[si].weight[step];
					calibresults[si].pos[step] = state[si].pos[step] / state[si].weight[step];
					calibresults[ti].pres[step] = state[ti].pres[step] / state[ti].weight[step];
					calibresults[ti].pos[step] = state[ti].pos[step] / state[ti].weight[step];
					if (step <= 4) {
						errors &= ~(1 << si);
						if (amax - amin < 1000) {
							snprintf(helptext, sizeof(helptext), "!pad %d upper not conn\ncheck soldering", si + 1);
							errors |= (1 << si);
						}
						else if (bmax - bmin < 1000) {
							snprintf(helptext, sizeof(helptext), "!pad %d lower not conn\ncheck soldering", si + 1);
							errors |= (1 << si);
						}
						else if (abs(calibresults[si].pos[step] - calibresults[si].pos[7]) < 300) {
							snprintf(helptext, sizeof(helptext), "!pad %d shorted?\ncheck soldering", si + 1);
							errors |= (1 << si);
						}
					}
					DebugLog("\n");
					curstep[si]--;
				}
				else
					readymask |= 1 << si; // flash the next finger if we want them to move on
			}
			if (step < 0)
				done++;
			prevrawpressure[si] = rawpressure;
		}
		if (done < 18)
			done = 0;
		int flash = triangle(millis());
		for (int fi = 0; fi < 9; ++fi) {
			int ready = readymask & (1 << fi);
			bool err = (errors & (1 << fi));
			for (int x = 0; x < 8; ++x) {
				int k = 0;
				if (x == curstep[fi])
					k = ready ? flash : 255 - state[fi].weight[x] * 12.f;
				if (err)
					k = maxi(k, flash / 2);
				leds[fi][x] = led_add_gamma(k);
			}
		}
	} // calibration loop
}
