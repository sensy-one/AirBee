#pragma once

#include <stdbool.h>

void airbee_mode_init(void);
bool airbee_mode_is_bluetooth(void);
bool airbee_mode_calibration_active(void);
void airbee_mode_calibration_complete(bool success);
