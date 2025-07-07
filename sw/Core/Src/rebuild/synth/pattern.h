#pragma once
#include "sequencer.h"
#include "utils.h"

// cleanup
extern u8 rampattern_idx;
extern u8 cur_pattern;
extern PatternQuarter rampattern[NUM_QUARTERS];
// -- cleanup

static PatternStringStep* get_string_step(u8 string_id) {
	// difference between current and ram pattern? not quite sure how/when this would happen
	if (rampattern_idx != cur_pattern)
		return 0;
	PatternStringStep* step = &rampattern[(cur_seq_step >> 4) & 3].steps[cur_seq_step & 15][string_id];
	// return pointer if any of its substeps hold pressure
	for (u8 substep_id = 0; substep_id < 8; substep_id++)
		if (step->pres[substep_id])
			return step;
	// otherwise return 0
	return 0;
}
