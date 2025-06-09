#include "config.h"
#include "gfx/data/names.h"
#include "gfx/gfx.h"
#include "hardware/accelerometer.h"
#include "hardware/adc_dac.h"
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
#include "synth/time.h"
#include "ui/led_viz.h"
#include "ui/oled_viz.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

// main loop frame

void plinky_frame(void) {
	codec_setheadphonevol(sys_params.headphonevol + 45);

	PumpWebUSB(false);

	// if (g_disable_fx) {
	// 	// web usb is up to its tricks :)
	// 	void draw_webusb_ui(int);
	// 	draw_webusb_ui(0);
	// 	HAL_Delay(30);
	// 	return;
	// }

	// handle spi flash writes for the sampler
	if (ui_mode == UI_SAMPLE_EDIT) {
		switch (sampler_mode) {
		case SM_ERASING:
			// this fully blocks the loop until the sample is erased, also draws its own oled visuals in the mean time
			clear_flash_sample();
			break;
		case SM_RECORDING:
		case SM_STOPPING1:
		case SM_STOPPING2:
		case SM_STOPPING3:
		case SM_STOPPING4:
			// pump blocks of the ram delay buffer to spi flash
			write_flash_sample_blocks();
		default:
			break;
		}
	}

	take_param_snapshots();
	draw_oled_visuals();
	draw_led_visuals();

	// reading the accelerometer needs to live in the main thread because it involves (blocking) I2C communication
	accel_read();

	// always do a ram_frame, except when in ui_sample_edit *and* working on a new sample
	if (sampler_mode == SM_PREVIEW)
		ram_frame();
}
