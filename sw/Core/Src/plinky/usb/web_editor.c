#include "web_editor.h"
#include "hardware/flash.h"
#include "hardware/ram.h"
#include "tusb.h"

/* webusb wire format. 10 byte header, then data.
u32 magic = 0xf30fabca
u8 cmd // 0 = get, 1=set
u8 idx // 0
u8 idx2 // 0
u8 idx3 // 0
u16 datalen // in bytes, not including this header, 0 for get, 1552 for set */

typedef struct WebUSBHeader { // 10 byte header
	u8 magic[4];
	u8 cmd;
	u8 idx;
	union {
		struct {
			u16 offset_16;
			u16 len_16;
		};
		struct { // these are valid if magic3 is 0xcc
			u32 offset_32;
			u32 len_32;
		};
	};
} __attribute__((packed)) WebUSBHeader;

// state machine ticks thru these in order, more or less
typedef enum WuState {
	WU_MAGIC0,
	WU_MAGIC1,
	WU_MAGIC2,
	WU_MAGIC3,
	WU_RCV_HDR,
	WU_RCV_DATA,
	WU_SND_HDR,
	WU_SND_DATA,
} WuState;

const static u8 magic[4] = {0xf3, 0x0f, 0xab, 0xca};
const static u8 magic_32[4] = {0xf3, 0x0f, 0xab, 0xcb}; // 32 bit version

static WuState state = WU_MAGIC0;   // current state
static WebUSBHeader header;         // header of current command
static u8* data_buf = (u8*)&header; // buffer where we are reading/writing atm
static u32 remaining_bytes = 1;     // how much left to read/write before state transition

static inline bool is_wu_hdr_32bit(void) {
	return header.magic[3] == magic_32[3];
}

static inline u32 wu_hdr_len(void) {
	return is_wu_hdr_32bit() ? header.len_32 : header.len_16;
}

static inline u32 wu_hdr_offset(void) {
	return is_wu_hdr_32bit() ? header.offset_32 : header.offset_16;
}

static void set_state(WuState new_state, u8* data, s32 len) {
	state = new_state;
	data_buf = data;
	remaining_bytes = len;
}

void web_editor_reset(void) {
	set_state(WU_MAGIC0, header.magic, 1);
}

void web_editor_frame(void) {
	u32 start_time = micros();
	while (micros() - start_time < 1000) {
		// process bytes
		tud_task();
		u32 handled_bytes = 0;
		switch (state) {
		// send bytes
		case WU_SND_HDR:
		case WU_SND_DATA:
			handled_bytes = tud_vendor_write(data_buf, remaining_bytes);
			break;
		// read bytes
		default:
			handled_bytes = tud_vendor_read(data_buf, mini(remaining_bytes, CFG_TUD_VENDOR_RX_BUFSIZE));
			break;
		}
		// nothing read or sent: buffer full or no data => try again
		if (handled_bytes == 0)
			continue;

		// progress
		remaining_bytes = remaining_bytes - handled_bytes;
		data_buf += handled_bytes;

		// more bytes to process
		if (remaining_bytes)
			continue;

		// move to next state
		switch (state) {
		// magic bytes
		case WU_MAGIC0:
		case WU_MAGIC1:
		case WU_MAGIC2:
		case WU_MAGIC3: {
			u8 m = header.magic[state];
			// received incorrect magic byte
			if (m != magic[state] && m != magic_32[state]) {
				// received magic 0, manually set state to magic 1
				if (m == magic[0]) {
					set_state(WU_MAGIC1, header.magic + 1, 1);
					break;
				}
				// reset to init state
				web_editor_reset();
				break;
			}
			state++;
			remaining_bytes = 1;
			// time to get rest of header
			if (state == WU_RCV_HDR)
				remaining_bytes = is_wu_hdr_32bit() ? 10 : 6;
			break;
		}
		// header received
		case WU_RCV_HDR:
			// only accept valid presets
			if (header.idx >= NUM_PRESETS) {
				web_editor_reset();
				break;
			}
			switch (header.cmd) {
			// request to send
			case 0:
				header.cmd = 1;
				if (wu_hdr_len() == 0) {
					u32 offset = wu_hdr_offset();
					header.len_16 = sizeof(Preset) - offset;
					header.offset_16 = offset;
					header.magic[3] = magic[3]; // 16 bit mode
				}
				set_state(WU_SND_HDR, (u8*)&header, is_wu_hdr_32bit() ? 14 : 10);
				break;
			// request to save
			case 1:
				load_preset(header.idx, false);
				set_state(WU_RCV_DATA, ((u8*)&cur_preset) + wu_hdr_offset(), wu_hdr_len());
				break;
			}
			break;
		// finished receiving data
		case WU_RCV_DATA:
			if (header.cmd == 1 && header.idx < NUM_PRESETS)
				log_ram_edit(SEG_PRESET);
			web_editor_reset();
			break;
		// we sent the header, now send the data
		case WU_SND_HDR:
			u8* data = header.idx == sys_params.preset_id ? (u8*)&cur_preset : (u8*)preset_flash_ptr(header.idx);
			set_state(WU_SND_DATA, data + wu_hdr_offset(), wu_hdr_len());
			break;
		// done sending data
		case WU_SND_DATA:
			web_editor_reset();
			break;
		default:
			break;
		}
	}
}