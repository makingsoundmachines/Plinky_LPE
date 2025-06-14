#pragma once
#include "synth/params.h"
#include "utils.h"

#define NUM_MIDI_CHANNELS 16

// to be organised in synth
extern u8 midi_pressure_override;
extern u8 midi_pitch_override;
extern u8 midi_suppress;
extern u16 memory_position[8];
extern u8 midi_notes[8];
extern u16 midi_positions[8];
extern u8 midi_velocities[8];
extern u8 midi_poly_pressure[8];
extern u8 midi_channels[8];
extern u8 midi_goal_note[8];
// -- to be organised in synth

extern u8 midi_chan_pressure[NUM_MIDI_CHANNELS];
extern s16 midi_chan_pitchbend[NUM_MIDI_CHANNELS];

void midi_init(void);
void process_all_midi_out(void);
void process_serial_midi_in(void);
void process_usb_midi_in(void);

// this needs a once-over for range & type after the synth is reorganised
static inline s32 midi_note_to_pitch(u8 midi_note, u8 midi_channel) {
	return ((midi_note - 24) << 9) + midi_chan_pitchbend[midi_channel] / 8;
}
