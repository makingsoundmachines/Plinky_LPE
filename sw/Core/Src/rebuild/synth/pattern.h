#pragma once
#include "sequencer.h"
#include "strings.h"
#include "utils.h"

#define MAX_PTN_STEPS 64
#define PTN_STEPS_PER_QTR (MAX_PTN_STEPS / NUM_QUARTERS)
#define PTN_SUBSTEPS 8

typedef struct PatternStringStep {
	u8 pos[PTN_SUBSTEPS / 2];
	u8 pres[PTN_SUBSTEPS];
} PatternStringStep;

typedef struct PatternQuarter {
	PatternStringStep steps[PTN_STEPS_PER_QTR][NUM_STRINGS];
	s8 autoknob[PTN_STEPS_PER_QTR * PTN_SUBSTEPS][NUM_KNOBS];
} PatternQuarter;

// these should be incorporated in memory module:
// static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
// static_assert((sizeof(PatternQuarter) & 15) == 0, "?");

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
