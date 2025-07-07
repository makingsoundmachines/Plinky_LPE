#include "hardware/adc_dac.h"
#include "hardware/touchstrips.h"
#include "synth/sampler.h"
#include "synth/strings.h"
#include "ui/pad_actions.h"
#include "ui/shift_states.h"
#include "ui/ui.h"

typedef struct CalibProgress {
	float weight[8];
	float pos[8];
	float pres[8];
} CalibProgress;
static inline CalibProgress* GetCalibProgress(int sensoridx) {
	CalibProgress* p = (CalibProgress*)delay_ram_buf;
	return p + sensoridx;
}
CalibResult calibresults[18];

static inline u8 getwaveform4halfres(SampleInfo* s, int x) { // x 0-1023
	u8 b = s->waveform4_b[x & 1023];
	return maxi(b & 15, b >> 4);
}
