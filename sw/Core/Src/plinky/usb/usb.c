#include "usb.h"
#include "tusb.h"
#include "web_editor.h"

extern bool web_serial_connected; // tinyusb/src/usbmidi.c
static bool web_serial_was_connected;

void init_usb(void) {
	tusb_init();
}

void usb_frame(void) {
	if (web_serial_connected != web_serial_was_connected) {
		// web editor reconnected => reset
		if (web_serial_connected)
			web_editor_reset();
		web_serial_was_connected = web_serial_connected;
	}

	if (web_serial_connected)
		// this throttles usb midi data as a side-effect
		web_editor_frame();
	else
		// throttle usb midi data manually
		tud_task();
}