#pragma once
#include "utils.h"

extern I2C_HandleTypeDef hi2c2;

// SSD1306 display utilities

#define SSD1306_SETLOWCOLUMN 0x00
#define SSD1306_EXTERNALVCC 0x01
#define SSD1306_SWITCHCAPVCC 0x02
#define SSD1306_SETHIGHCOLUMN 0x10
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22
#define SSD1306_DEACTIVATE_SCROLL 0x2E
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_DISPLAYALLON 0xA5
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY 0xA6
#define SSD1306_INVERTDISPLAY 0xA7
#define SSD1306_SETCONTRAST 0x81
#define SSD1306_CHARGEPUMP 0x8D
#define SSD1306_COMSCANINC 0xC0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF

#define I2C_ADDRESS (0x3c << 1)

static inline void ssd1306_wait(void) {
	while (HAL_I2C_GetState(&hi2c2) == HAL_I2C_STATE_BUSY_TX)
		;
}

static inline void ssd1306_command(unsigned char c) {
	u8 buf[2] = {0, c};
	HAL_I2C_Master_Transmit(&hi2c2, I2C_ADDRESS, buf, 2, 20);
	HAL_Delay(1);
}

static inline void ssd1306_flip(u8* buffer) {
	ssd1306_wait();
	ssd1306_command(0); // Page start address (0 = reset)
	HAL_I2C_Master_Transmit(&hi2c2, I2C_ADDRESS, buffer, OLED_BUFFER_SIZE, 500);
}

static void ssd1306_init() {
	// Init sequence
	ssd1306_command(SSD1306_DISPLAYOFF);         // 0xAE
	ssd1306_command(SSD1306_SETDISPLAYCLOCKDIV); // 0xD5
	ssd1306_command(0x80);                       // the suggested ratio 0x80

	ssd1306_command(SSD1306_SETMULTIPLEX); // 0xA8
	ssd1306_command(OLED_HEIGHT - 1);

	ssd1306_command(SSD1306_SETDISPLAYOFFSET);   // 0xD3
	ssd1306_command(0x0);                        // no offset
	ssd1306_command(SSD1306_SETSTARTLINE | 0x0); // line #0
	ssd1306_command(SSD1306_CHARGEPUMP);         // 0x8D
	ssd1306_command(0x14);                       // switchcap
	ssd1306_command(SSD1306_MEMORYMODE);         // 0x20
	ssd1306_command(0x00);                       // 0x0 act like ks0108
	ssd1306_command(SSD1306_SEGREMAP | 0x1);
	ssd1306_command(SSD1306_COMSCANDEC);

	ssd1306_command(SSD1306_SETCOMPINS); // 0xDA
	ssd1306_command(0x02);
	ssd1306_command(SSD1306_SETCONTRAST); // 0x81
	ssd1306_command(0x8F);

	ssd1306_command(SSD1306_SETPRECHARGE);  // 0xd9
	ssd1306_command(0xF1);                  // switchcap
	ssd1306_command(SSD1306_SETVCOMDETECT); // 0xDB
	ssd1306_command(0x40);
	ssd1306_command(SSD1306_DISPLAYALLON_RESUME); // 0xA4
	ssd1306_command(SSD1306_NORMALDISPLAY);       // 0xA6

	ssd1306_command(SSD1306_DEACTIVATE_SCROLL);

	// prepare flip
	ssd1306_command(SSD1306_COLUMNADDR);
	ssd1306_command(0);              // Column start address (0 = reset)
	ssd1306_command(OLED_WIDTH - 1); // Column end address (127 = reset)
	ssd1306_command(SSD1306_PAGEADDR);
	ssd1306_command(0);                 // Page start address (0 = reset)
	ssd1306_command(3);                 // Page end address - 32 pixels. 1=16 pixels
	ssd1306_command(SSD1306_DISPLAYON); //--turn on oled panel
}