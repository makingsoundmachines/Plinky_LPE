#pragma once
#include "utils.h"

float accel_get_axis(bool y_axis);

void init_accel(void);
void accel_read(void);
void accel_tick(void);