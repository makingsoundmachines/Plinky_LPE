#if defined(_WIN32) || defined(__APPLE__)
// #define EMU
#pragma warning(disable : 4244)
#endif

#ifdef WASM
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifndef EMU
#include <main.h>

extern DMA_HandleTypeDef hdma_adc1;

extern DMA_HandleTypeDef hdma_dac_ch1;
extern DMA_HandleTypeDef hdma_dac_ch2;

extern I2C_HandleTypeDef hi2c2;

extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockB1;
extern DMA_HandleTypeDef hdma_sai1_a;
extern DMA_HandleTypeDef hdma_sai1_b;

extern SPI_HandleTypeDef hspi2;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern DMA_HandleTypeDef hdma_spi2_rx;

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

#endif

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define IMPL
#ifdef WASM
#define ASSERT(...)
#else
#define ASSERT assert
#endif
#include "core.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
#include "hardware/codec.h"
#include "hardware/flash.h"
#include "hardware/leds.h"
#include "hardware/midi.h"
#include "hardware/ram.h"
#include "hardware/spi.h"
#include "synth/arp.h"
#include "synth/audio.h"
#include "synth/sampler.h"
#include "synth/sequencer.h"
#include "synth/synth.h"
#include "synth/time.h"
#include "testing/tick_counter.h"
#include "ui/oled_viz.h"
#include "ui/ui.h"

#define TWENTY_OVER_LOG2_10 6.02059991328f // (20.f/log2(10.f));

static inline float lin2db(float lin) {
	return log2f(lin) * TWENTY_OVER_LOG2_10;
}
static inline float db2lin(float db) {
	return exp2f(db * (1.f / TWENTY_OVER_LOG2_10));
}

TickCounter _tc_budget;
TickCounter _tc_all;
TickCounter _tc_fx;
TickCounter _tc_audio;
TickCounter _tc_touch;
TickCounter _tc_led;
TickCounter _tc_filter;

// u8 ui_edit_param_prev[2][4] = {
// {NUM_PARAMS,NUM_PARAMS,NUM_PARAMS,NUM_PARAMS},{NUM_PARAMS,NUM_PARAMS,NUM_PARAMS,NUM_PARAMS} }; // push to front
// history

/*

update
kick fetch for this pos
*/

// these includes are sensitive to how they are ordered
// turning off formatting so that they don't get reordered alphabetically

// clang-format off

#include "touch.h"
#include "calib.h"

#include "../webusb.h"

// clang-format on

inline s32 trifold(u32 x) {
	if (x > 0x80000000)
		x = 0xffffffff - x;
	return (s32)(x >> 4);
}

#ifdef EMU
float arpdebug[1024];
int arpdebugi;
#endif

#ifdef EMU
float powerout; // squared power
float gainhistoryrms[512];
int ghi;
#endif

#ifdef EMU
float m_compressor, m_dry, m_audioin, m_dry2wet, m_delaysend, m_delayreturn, m_reverbin, m_reverbout, m_fxout, m_output;
void MONITORPEAK(float* mon, u32 stereoin) {
	STEREOUNPACK(stereoin);
	float peak = (1.f / 32768.f) * maxi(abs(stereoinl), abs(stereoinr));
	if (peak > *mon)
		*mon = peak;
	else
		*mon += (peak - *mon) * 0.0001f;
}
#endif

#ifdef DEBUG
// #define NOISETEST
#endif
#ifdef NOISETEST
float noisetestl = 0, noisetestr = 0, noisetest = 0;
#endif

void DoAudio(u32* audio_out, u32* audio_in) {
	tc_start(&_tc_audio);
	audio_pre(audio_out, audio_in);

	// in the process of recording a new sample
	if (sampler_mode > SM_PREVIEW) {
		sampler_recording_tick(audio_out, audio_in);
		return; // skip all other synth functionality
	}

	//////////////////////////////////////////////////////////
	// PLAYMODE

	update_preset_ram(false);
	// a few midi messages per tick. WCGW
	process_all_midi_out();
#ifndef EMU
	PumpWebUSB(true);
	process_serial_midi_in();
#endif
	process_usb_midi_in();

	// do the clock first so we can update the sequencer step etc
	clock_tick();

	seq_tick();

	generate_string_touches();

	arp_tick();

	params_tick();

	update_sample_ram(false);
	update_pattern_ram(false);

	// synth

	handle_synth_voices(audio_out);

	tc_stop(&_tc_audio);

	spi_tick();

	tc_start(&_tc_fx);

	audio_post(audio_out, audio_in);
}

/////////////////////////////////////////////////////////

#ifdef EMU
uint32_t emupixels[128 * 32];
void OledFlipEmu(const u8* vram) {
	if (!vram)
		return;
	const u8* src = vram + 1;
	for (int y = 0; y < 32; y += 8) {
		for (int x = 0; x < 128; x++) {
			u8 b = *src++;
			for (int yy = 0; yy < 8; ++yy) {
				u32 c = (b & 1) ? 0xffffffff : 0xff000000;
				int y2 = y + yy;
#ifdef ROTATE_OLED
				emupixels[((31 - y2) + x * 32)] = c; // rotated, pins at top
#else
				emupixels[(y2 * 128 + x)] = c;
#endif
				b >>= 1;
			}
		}
	}
}

int* EMSCRIPTEN_KEEPALIVE getemubitmap(void) {
	return (int*)emupixels;
}
uint8_t* EMSCRIPTEN_KEEPALIVE getemuleds() {
	return (uint8_t*)leds;
}

#endif

void EMSCRIPTEN_KEEPALIVE uitick(u32* audio_out, const u32* audio_in, int half) {
	tc_stop(&_tc_budget);
	tc_start(&_tc_budget);

	tc_start(&_tc_all);
	//	if (half)
	{
		tc_start(&_tc_touch);
		ui_frame();
		tc_stop(&_tc_touch);
	}
	//	else
	{
		tc_start(&_tc_led);
		leds_update();
		tc_stop(&_tc_led);
	}

	// clear some scope pixels

	// pass thru: memcpy(audio_out,audio_in,64*4);

	DoAudio((u32*)audio_out, (u32*)audio_in);
	tc_stop(&_tc_all);

#ifdef RB_SPEEDTEST
	// display _tc_all on oled
	static const u16 log_cycle = 5000;
	static u32 speed_log = 0;
	if (do_every(log_cycle, &speed_log))
		tc_gfx_log(&_tc_all, "all");
	static u32 _tc_all_log = 0;
	if (do_every(log_cycle / 2, &_tc_all_log))
		tc_reset(&_tc_all);
#endif
}

static void jumptobootloader(void) {
	// todo - maybe set a flag in the flash and then use NVIC_SystemReset() which will cause it to jumptobootloader
	// earlier https://community.st.com/s/question/0D50X00009XkeeW/stm32l476rg-jump-to-bootloader-from-software
	typedef void (*pFunction)(void);
	pFunction JumpToApplication;
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	__disable_irq();
	__DSB();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); /* Remap is bot visible at once. Execute some unrelated command! */
	__DSB();
	__ISB();
	JumpToApplication = (void (*)(void))(*((uint32_t*)(0x1FFF0000 + 4)));
	__set_MSP(*(__IO uint32_t*)0x1FFF0000);
	JumpToApplication();
}

void reflash(void) {
	oled_clear();
	draw_str(0, 0, F_16_BOLD, "Re-flash");
	draw_str(0, 16, F_16, "over USB DFU");
	oled_flip();
	HAL_Delay(100);
	jumptobootloader();
}

void set_test_mux(int c) {
#ifndef EMU
	GPIOD->ODR &= ~(GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4); // rgb led off
	if (c & 1)
		GPIOD->ODR |= GPIO_PIN_3;
	if (c & 2)
		GPIOD->ODR |= GPIO_PIN_1;
	if (c & 4)
		GPIOD->ODR |= GPIO_PIN_4;
	if (c & 8)
		GPIOA->ODR |= GPIO_PIN_8;
	else
		GPIOA->ODR &= ~GPIO_PIN_8;
#endif
}
void set_test_rgb(int c) {
	set_test_mux(c ^ 7);
}

// short* getrxbuf(void);

void check_bootloader_flash(void) {
	int count = 0;
	uint32_t* rb32 = (uint32_t*)reverb_ram_buf;
	uint32_t magic = rb32[64];
	char* rb = (char*)reverb_ram_buf;
	for (; count < 64; ++count)
		if (rb[count] != 1)
			break;
	DebugLog("bootloader left %d ones for us magic is %08x\r\n", count, magic);
	const uint32_t* app_base = (const uint32_t*)delay_ram_buf;

	if (count != 64 / 4 || magic != 0xa738ea75) {
		return;
	}
	char buf[32];
	// checksum!
	uint32_t checksum = 0;
	for (int i = 0; i < 65536 / 4; ++i) {
		checksum = checksum * 23 + ((uint32_t*)delay_ram_buf)[i];
	}
	if (checksum != GOLDEN_CHECKSUM) {
		DebugLog("bootloader checksum failed %08x != %08x\r\n", checksum, GOLDEN_CHECKSUM);
		oled_clear();
		draw_str(0, 0, F_8, "bad bootloader crc");
		snprintf(buf, sizeof(buf), "%08x vs %08x", (unsigned int)checksum, (unsigned int)GOLDEN_CHECKSUM);
		draw_str(0, 8, F_8, buf);
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	oled_clear();
	snprintf(buf, sizeof(buf), "%08x %d", (unsigned int)magic, count);
	draw_str(0, 0, F_16, buf);
	snprintf(buf, sizeof(buf), "%08x %08x", (unsigned int)app_base[0], (unsigned int)app_base[1]);
	draw_str(0, 16, F_12, buf);
	oled_flip();

	rb32[64]++; // clear the magic

	DebugLog("bootloader app base is %08x %08x\r\n", (unsigned int)app_base[0], (unsigned int)app_base[1]);

	/*
	 * We refuse to program the first word of the app until the upload is marked
	 * complete by the host.  So if it's not 0xffffffff, we should try booting it.
	 */
	if (app_base[0] == 0xffffffff || app_base[0] == 0) {
		HAL_Delay(10000);
		return;
	}

	// first word is stack base - needs to be in RAM region and word-aligned
	if ((app_base[0] & 0xff000003) != 0x20000000) {
		HAL_Delay(10000);
		return;
	}

	/*
	 * The second word of the app is the entrypoint; it must point within the
	 * flash area (or we have a bad flash).
	 */
	if (app_base[1] < 0x08000000 || app_base[1] >= 0x08010000) {
		HAL_Delay(10000);
		return;
	}
	DebugLog("FLASHING BOOTLOADER! DO NOT RESET\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "FLASHING\nBOOTLOADER");
	char verbuf[5] = {};
	memcpy(verbuf, (void*)(delay_ram_buf + 65536 - 4), 4);
	draw_str(0, 24, F_8, verbuf);

	oled_flip();
	HAL_FLASH_Unlock();
	FLASH_EraseInitTypeDef EraseInitStruct;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.Page = 0;
	EraseInitStruct.NbPages = 65536 / 2048;
	uint32_t SECTORError = 0;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
		DebugLog("BOOTLOADER flash erase error %d\r\n", SECTORError);
		oled_clear();
		draw_str(0, 0, F_16_BOLD, "BOOTLOADER\nERASE ERROR");
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	DebugLog("BOOTLOADER flash erased ok!\r\n");

	__HAL_FLASH_DATA_CACHE_DISABLE();
	__HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
	__HAL_FLASH_DATA_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
	__HAL_FLASH_DATA_CACHE_ENABLE();
	uint64_t* s = (uint64_t*)delay_ram_buf;
	volatile uint64_t* d = (volatile uint64_t*)0x08000000;
	u32 size_bytes = 65536;
	for (; size_bytes > 0; size_bytes -= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (uint32_t)(size_t)(d++), *s++);
	}
	HAL_FLASH_Lock();
	DebugLog("BOOTLOADER has been flashed!\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "BOOTLOADER\nFLASHED OK!");
	draw_str(0, 24, F_8, verbuf);
	oled_flip();
	HAL_Delay(3000);
}

#undef ERROR
#define ERROR(msg, ...)                                                                                                \
	do {                                                                                                               \
		errorcount++;                                                                                                  \
		DebugLog("\r\n" msg "\r\n", __VA_ARGS__);                                                                      \
	} while (0)

void test_jig(void) {
	// pogo pin layout:
	// GND DEBUG = GND / PA8 - 67
	// GND GND
	// MISO SPICLK = PD3 - 84 / PD1 - 82
	// MOSI GND = PD4 - 85 / GND
	// rgb led is hooked to PD1,PD3,PD4. configure it for output
	// mux address hooked to LSB=PD3, PD1, PD4, PA8=MSB
#ifndef EMU
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	// we also use debug as an output now!
	GPIO_InitStruct.Pin = GPIO_PIN_8;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	GPIOA->ODR &= ~GPIO_PIN_8;
#endif
	oled_clear();
	draw_str(0, 0, F_32_BOLD, "TEST JIG");
	oled_flip();
	HAL_Delay(100);
	send_cv_trigger(false);
	send_cv_clock(false);
	send_cv_gate(0);
	send_cv_pressure(0);
	int gndcalib[ADC_CHANS] = {0};
	int refcalib[ADC_CHANS] = {0};
	float pdac[2][2] = {0};
#define PITCH_1V_OUT (43000 - 8500) // about 8500 per volt; 43000 is zero ish.
#define PITCH_4V_OUT (43000 - 8500 * 4)
	static int const expected_mvolts[11][2] = {
	    {0, 0},       // gnd
	    {2500, 2500}, // 2.5 ref
	    {3274, 3274}, // 3.3 supply
	    {4779, 4779}, // 5v supply
	    {950, 950},   // 1v from 12v supply
	    {1039, 4230}, // pitch lo 1v/4v
	    {1039, 4230}, // pitch hi 1v/4v
	    {0, 4700},    // clock
	    {0, 4700},    // trig,
	    {0, 4700},    // gate,
	    {0, 4700},    // pressure
	};
	static int const tol_mvolts[11] = {
	    100,                // gnd
	    10,                 // ref
	    300,                // 3.3
	    500,                // 5
	    100,                // 1v
	    100, 100,           // pitch
	    150, 150, 150, 150, // outputs
	};
	// const char* const names[11][2] = {{"gnd", 0},
	//                                   {"2.5v", 0},
	//                                   {"3.3v", 0},
	//                                   {"5v", 0},
	//                                   {"1v from 12v", 0},
	//                                   {"plo (1v)", "plo (4v)"},
	//                                   {"phi (1v)", "phi (4v)"},
	//                                   {"clk (0v)", "clk (4.6v)"},
	//                                   {"trig (0v)", "trig (4.6v)"},
	//                                   {"gate (0v)", "gate (4.6v)"},
	//                                   {"pressure (0v)", "pressure (4.6v)"}};
	while (1) {
		DebugLog("mux in:  pitch     gate      x        y        a        b   | mux:\r\n");
		int errorcount = 0;
		int rangeok = 0, zerook = 0;
		// reset_ext_clock();
		for (int iter = 0; iter < 4; ++iter) {
			send_cv_clock(false);
			HAL_Delay(3);
			send_cv_clock(true);
			HAL_Delay(3);
		}
		// if (ext_clock_counter != 4)
		// 	ERROR("expected clkin of 4, got %d", ext_clock_counter);
		for (int mux = 0; mux < 11; ++mux) {
			set_test_mux(mux);
			int numlohi = (mux < 5) ? 1 : 2;
			for (int lohi = 0; lohi < numlohi; ++lohi) {
				int data = lohi ? 49152 : 0;
				int pitch = lohi ? PITCH_4V_OUT : PITCH_1V_OUT;
				send_cv_trigger(data > 0 ? true : false);
				send_cv_clock(data > 0 ? true : false);
				send_cv_gate(data);
				send_cv_pressure(data);
				send_cv_pitch_lo(pitch, 0);
				send_cv_pitch_hi(pitch, 0);

				HAL_Delay(3);
				int tot[ADC_CHANS] = {0};
				oled_clear();

#define NUMITER 32
				for (int iter = 0; iter < NUMITER; ++iter) {
					HAL_Delay(2);
					// short* rx = getrxbuf();
					// for (int x = 0; x < 128; ++x) {
					// 	put_pixel(x, 16 + rx[x * 2] / 1024, 1);
					// 	put_pixel(x, 16 + rx[x * 2 + 1] / 1024, 1);
					// }
					for (int j = 0; j < ADC_SAMPLES; ++j)
						for (int ch = 0; ch < ADC_CHANS; ++ch)
							tot[ch] += adc_buffer[j * ADC_CHANS + ch];
				}
				if (lohi)
					inverted_rectangle(0, 0, 128, 32);
				oled_flip();
				for (int ch = 0; ch < ADC_CHANS; ++ch) {
					tot[ch] /= ADC_SAMPLES * NUMITER;
				}
				DebugLog("-----\nmux = %d lohi = %d\n", mux, lohi);
				for (int ch = 0; ch < ADC_CHANS; ++ch) {
					DebugLog("adc ch reads %d\n", tot[ch]);
				}
				DebugLog("-----\n");
				switch (mux) {
				case 0:
					for (int ch = 0; ch < ADC_CHANS; ++ch) {
						gndcalib[ch] = tot[ch];
						int expected = (ch == 0) ? 43262 : (ch >= 6) ? 31500 : 31035;
						int error = abs(expected - tot[ch]);
						if (error > 2000)
							ERROR("ADC Channel %d zero point is %d, expected %d", ch, tot[ch], expected);
						else
							zerook |= (1 << ch);
					}
					break;
				case 1:
					for (int ch = 0; ch < ADC_CHANS; ++ch) {
						refcalib[ch] = tot[ch];
						int range = gndcalib[ch] - refcalib[ch];
						int expected = (ch == 0) ? 21600 : (ch >= 6) ? 0 : 14386;
						int error = abs(expected - range);
						if (error > 2000)
							ERROR("ADC Channel %d range is %d, expected %d", ch, range, expected);
						else
							rangeok |= (1 << ch);
					}
					break;
				case 5:
				case 6: {
					int range0 = (refcalib[0] - gndcalib[0]);
					if (range0 == 0)
						range0 = 1;
					pdac[mux - 5][lohi] = (tot[0] - gndcalib[0]) * (2.5f / range0);
					break;
				}
				}
				DebugLog("%4d: ", mux);
				for (int ch = 0; ch < 6; ++ch) {
					int range = (refcalib[ch] - gndcalib[ch]);
					if (range == 0)
						range = 1;
					float gain = 2500.f / range;
					int mvolts = (tot[ch] - gndcalib[ch]) * gain;
					int exp_mvolts = expected_mvolts[mux][lohi];
					int error = abs(exp_mvolts - mvolts);
					int tol = tol_mvolts[mux];
					bool ok = true;
					if (error > tol) {
						ok = false;
						ERROR("ADC channel %d was %dmv, expected %dmv, outside tolerence of %dmv", ch, mvolts,
						      exp_mvolts, tol);
					}
					DebugLog("%6dmv%c ", mvolts, ok ? ' ' : '*');
				}
				// rj: logging should be re-implemented with custom clock counter
				// DebugLog("| %s. clocks=%d\r\n", names[mux][lohi], ext_clock_counter);
			}
		}
		DebugLog("zero: ");
		for (int ch = 0; ch < 8; ++ch)
			DebugLog("%6d%c   ", gndcalib[ch], (zerook & (1 << ch)) ? ' ' : '*');
		DebugLog("\r\nrange ");
		for (int ch = 0; ch < 6; ++ch)
			DebugLog("%6d%c   ", gndcalib[ch] - refcalib[ch], (rangeok & (1 << ch)) ? ' ' : '*');
		DebugLog("\r\n%d errors\r\n\r\n", errorcount);
		set_test_rgb(errorcount ? 4 : 2);
		oled_clear();
		if (errorcount == 0)
			draw_str(0, 0, F_32_BOLD, "GOOD!");
		else
			fdraw_str(0, 0, F_32_BOLD, "%d ERRORS", errorcount);
		oled_flip();
		for (int ch = 0; ch < 8; ++ch) {
			int zero = gndcalib[ch];
			int range = gndcalib[ch] - refcalib[ch];
			if (range == 0)
				range = 1;
			adc_dac_calib[ch].bias = zero;
			if (ch >= 6)
				adc_dac_calib[ch].scale = -1.01f / (zero + 1);
			else if (ch == 0)
				adc_dac_calib[ch].scale =
				    -2.5f / range; // range is measured off 2.5; so this scales it so that we get true volts out
			else
				adc_dac_calib[ch].scale =
				    (-2.5f / 5.f)
				    / range; // range is measured off 2.5; so this scales it so that we get 1 out for 5v in
		}
		// ok pdac[k][0] tells us what we got from the ADC when we set the DAC to PITCH_1V_OUT, and pdac[k][1] tells
		// us what we got when we output PITCH_4V_OUT so we have dacb + dacs * plo0 = PITCH_1V_OUT and       dacb +
		// dacs * plo1 = PITCH_4V_OUT dacs = (PITCH_1V_OUT-PITCH_4V_OUT) / (plo0-plo1) dacb = PITCH_1V_OUT -
		// dacs*plo0
		for (int dacch = 0; dacch < 2; ++dacch) {
			float range = (pdac[dacch][0] - pdac[dacch][1]);
			if (range == 0)
				range = 1.f;
			float scale_per_volt = (PITCH_1V_OUT - PITCH_4V_OUT) / range;
			float zero = PITCH_1V_OUT - scale_per_volt * pdac[dacch][0];
			DebugLog("dac channel %d has zero at %d and %d steps per volt, should be around 42500 and -8000 ish\r\n",
			         dacch, (int)zero, (int)scale_per_volt);
			adc_dac_calib[dacch + 8].bias = zero;
			adc_dac_calib[dacch + 8].scale = scale_per_volt * (1.f / (2048.f * 12.f)); // 2048 per semitone
		}

		flash_writecalib(2);

		HAL_Delay(errorcount ? 2000 : 4000);
	}
}

void plinky_frame(void);

// #define BITBANG
#ifdef WASM

bool send_midimsg(u8 status, u8 data1, u8 data2) {
	return true;
}
void spi_update_dac(int chan) {
	resetspistate();
}

void EmuStartSound(void) {
}

bool usb_midi_receive(unsigned char packet[4]) {
	return false; // fill in packet and return true if midi found
}
int emutouch[9][2];
void EMSCRIPTEN_KEEPALIVE wasm_settouch(int idx, int pos, int pressure) {
	if (idx >= 0 && idx < 9)
		emutouch[idx][1] = pos, emutouch[idx][0] = pressure;
}

void EMSCRIPTEN_KEEPALIVE plinky_frame_wasm(void) {
	plinky_frame();
}
u32 wasmbuf[SAMPLES_PER_TICK];
uint8_t* EMSCRIPTEN_KEEPALIVE get_wasm_audio_buf(void) {
	return (uint8_t*)wasmbuf;
}
uint8_t* EMSCRIPTEN_KEEPALIVE get_wasm_preset_buf(void) {
	return (uint8_t*)&cur_preset;
}
void EMSCRIPTEN_KEEPALIVE wasm_audio(void) {
	static u8 half = 0;
	u32 audio_in[SAMPLES_PER_TICK] = {0};
	uitick(wasmbuf, audio_in, half);
	half = 1 - half;
}
void EMSCRIPTEN_KEEPALIVE wasm_pokepreset(int offset, int byte) {
	if (offset >= 0 && offset < sizeof(cur_preset))
		((u8*)&cur_preset)[offset] = byte;
}
int EMSCRIPTEN_KEEPALIVE wasm_peekpreset(int offset) {
	if (offset >= 0 && offset < sizeof(cur_preset))
		return ((u8*)&cur_preset)[offset];
	return 0;
}
int EMSCRIPTEN_KEEPALIVE wasm_getcurpreset(void) {
	return sys_params.curpreset;
}
void EMSCRIPTEN_KEEPALIVE wasm_setcurpreset(int i) {
	load_preset(i, false);
}
#endif

void EMSCRIPTEN_KEEPALIVE plinky_init(void) {
	accel_init();
	denormals_init();
	reset_touches();
	tc_init();

	HAL_Delay(100); // stablise power before bringing oled up
	gfx_init();     // also initializes oled
	check_bootloader_flash();
	audio_init();
	codec_init();
	adc_dac_init();

#ifdef EMU
	void EmuStartSound(void);
	EmuStartSound();
#endif

	// see if were in the testjig - it pulls PA8 (pin 67) down 'DEBUG'
#ifndef EMU
	if (!(GPIOA->IDR & (1 << 8))) {
		test_jig();
	}

	// turn debug pin to an output
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = GPIO_PIN_8;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	GPIOA->BSRR = 1 << 8; // cs high
	HAL_Delay(1);
	spi_init();
	midi_init();

#endif
	leds_init();

	int flashvalid = flash_readcalib();
	if (!(flashvalid & 1)) { // no calib at all
		reset_touches();
		calib();
		flashvalid |= 1;
		flash_writecalib(flashvalid);
	}
	if (!(flashvalid & 2)) {
		// cv_reset_calib();
		cv_calib();
		flashvalid |= 2;
		flash_writecalib(3);
	}
	HAL_Delay(80);
	int knoba = adc_get_raw(ADC_A_KNOB);
	int knobb = adc_get_raw(ADC_B_KNOB);
	leds_bootswish();
	knoba = abs(knoba - (int)adc_get_raw(ADC_A_KNOB));
	knobb = abs(knobb - (int)adc_get_raw(ADC_B_KNOB));
	DebugLog("knob turned by %d,%d during boot\r\n", knoba, knobb);
	//  turn knobs during boot to force calibration
#ifndef WASM
	if (knoba > 4096 || knobb > 4096) {
		if (knoba > 4096 && knobb > 4096) {
			// both knobs twist on boot - jump to stm flash bootloader
			reflash();
		}
		if (knoba > 4096) {
			// left knob twist on boot - full calib
			reset_touches();
			calib();
		}
		else {
			// right knob twist on boot - cv calib only
			cv_calib();
		}
		flash_writecalib(3);
	}
#endif
	init_flash();
	init_ram();

	sampler_mode = SM_PREVIEW;
}

#include "ui.h"
