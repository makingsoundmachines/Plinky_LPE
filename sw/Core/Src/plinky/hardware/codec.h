#pragma once
#include "utils.h"

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hi2s);
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hi2s);

void codec_init(void);
void codec_update_volume(void);
