#pragma once
#include "utils.h"

// position of the ADC reading in the adc_buffer
typedef enum ADC_DAC_Index {
	ADC_PITCH,
	ADC_GATE,
	ADC_X_CV,
	ADC_Y_CV,
	ADC_A_CV,
	ADC_B_CV,
	ADC_B_KNOB = 6,
	ADC_A_KNOB = 7,
	DAC_PITCH_CV_LO,
	DAC_PITCH_CV_HI,
	NUM_ADC_DAC_ITEMS,
} ADC_DAC_Index;

// position of the ADC value in the adc_smoother array
typedef enum ADCSmoothIndex {
	ADC_S_A_CV = 0,
	ADC_S_B_CV = 1,
	ADC_S_X_CV = 2,
	ADC_S_Y_CV = 3,
	ADC_S_A_KNOB = 4,
	ADC_S_B_KNOB = 5,
	ADC_S_PITCH = 6,
	ADC_S_GATE = 7,
} ADCSmoothIndex;
// rj: can we get rid of this remapping?

typedef struct ADC_DAC_Calib {
	float bias;
	float scale;
} ADC_DAC_Calib;

ADC_DAC_Calib* adc_dac_calib_ptr(void);

void adc_dac_init(void);

u16 adc_get_raw(ADC_DAC_Index index);
float adc_get_calib(ADC_DAC_Index index);
float adc_get_smooth(ADCSmoothIndex index);

s32 apply_dac_pitch_calib(bool pitch_hi, s32 pitch_uncalib);
s32 get_dac_pitch_octave(bool pitch_hi);

void adc_update_inputs(void);

// cv

extern TIM_HandleTypeDef htim3;

enum ECVQuant {
	CVQ_OFF,
	CVQ_ON,
	CVQ_SCALE,
	CVQ_LAST,
};

void send_cv_pitch(bool pitch_hi, s32 data, bool apply_calib);
void cv_calib(void);

static inline void send_cv_clock(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, high ? 255 : 0);
}

static inline void send_cv_trigger(bool high) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, high ? 255 : 0);
}

static inline void send_cv_gate(u16 data) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, data >> 8);
}

static inline void send_cv_pressure(u16 data) {
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, data >> 8);
}

// #define SENSE1_Pin GPIO_PIN_8
// #define SENSE1_GPIO_Port GPIOE
// #define SENSE2_Pin GPIO_PIN_15
// #define SENSE2_GPIO_Port GPIOE
//
// rj: this is ignoring MX_GPIO_Init() in main.c, could be cleaner after low level hardware setup cleanup

static inline bool cv_gate_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_8) == GPIO_PIN_RESET;
}

static inline bool cv_pitch_present(void) {
	return HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_15) == GPIO_PIN_RESET;
}