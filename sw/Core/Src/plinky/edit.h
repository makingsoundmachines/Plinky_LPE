#include "synth/params.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

#include "ui/pad_actions.h"
#include "synth/strings.h"

void toggle_arp(void) {
	rampreset.flags ^= FLAGS_ARP;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_ARP)) ? "arp on" : "arp off", 0);
	ramtime[GEN_SYS] = millis();
}
void toggle_latch(void) {
	rampreset.flags ^= FLAGS_LATCH;
	ShowMessage(F_32_BOLD, ((rampreset.flags & FLAGS_LATCH)) ? "latch on" : "latch off", 0);
	ramtime[GEN_SYS] = millis();
	if (!((rampreset.flags & FLAGS_LATCH)))
		clear_latch();
}