# Plinky Firmware - Lucky Phoenix Edition

This is a user-maintained branch of firmware for the Plinky and Plinky+ synths. For the official firmware and information about the Plinky devices, visit the [official firmware repo](https://github.com/plinkysynth/plinky_public) and [official website](https://plinkysynth.com)

## Current Release: v0.3.0 - *The one with the parameters*
**Release date:** 10 October 2025

**Important:**

&rarr; *This firmware affects the mapping of some parameters when it boots - backup your presets before installing!*

**Binary:**

&rarr; [PlinkyLPE-0.3.0.uf2](../../raw/dev/builds/PlinkyLPE-0.3.0.uf2)

**How to install:**
- Download the binary above
- Follow the firmware installation instructions on [the official website](https://plinkysynth.com/firmware)
- If you're updating from an older firmware version, it might be necessary to rename the file to CURRENT.UF2 before installing

*Please report any bugs as a [github issue](../../issues) or in the [#bug-reports](https://discord.com/channels/784856175937585152/844199535860383776) channel of the Plinky discord, so I can fix them*

---

### Release v0.3.0 highlights
This release provides a much cleaner way of interacting with the Plinky by implementing lots of small cleanups in how parameters are handled and displayed. Overall the experience should have less friction and feel more intuitive. Here are some of the most important changes:
#### Visual parameter cleanup
- Parameter categories, names and values no longer clash with other ui elements
- Parameter icons, categories and names have been cleaned up where necessary
- Parameter values use consistent font sizes and appropriate numbers of decimals
- Modulated values are shown on the bottom row and stay visible when editing modulation depth
- Time values are shown in intuitive fractions when synced and in (milli)seconds when unsynced
- The leds of the edit strip (leftmost colum) have been cleaned up
#### Functional parameter cleanup
- All parameters now use their full range
- All encoder-skips have been removed, each encoder turn now actually affects the parameter value
- The edit strip no longer forces a notch at the center for bipolar parameters
- Unused parameters can no longer be selected
- Some parameters are now mapped more intuitively
#### Updates
- Arp and Latch are now real parameters and can be modulated
- Arp, Seq and LFO synced timing now goes up to 32 bars
- Microtone is now scale-aware - at 100% you can slide smoothly between any two pads regardless of note-distance
- Holding shift now enables precision encoder editing - values move one step at a time
- A global settings menu has been added on the Settings pad, which currently holds:
    - Settings: accelerometer sensitivity, encoder direction, midi in/out channel, cv quantization mode
    - Actions: reboot, touch calibration, cv calibration, revert presets
#### Hardware/firmware compatibility
- This firmware now runs on Plinky+
- This firmware has some new functionality and a few parameters mapped differently than the OG firmware. On startup, presets saved on OG firmware will be updated to sound (practically) identical on LPE firmware
- The new settings menu has an action called "OG Presets", which will perform the inverse process - making presets play properly on OG firmware again

*There are many more improvements in this version, the full list can be found in the [release notes](RELEASE_NOTES.md)*


