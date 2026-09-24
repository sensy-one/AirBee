#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t sht45_raw_temperature;
  uint16_t sht45_raw_humidity;
  float pressure_pa;
  uint16_t co2_ppm;
  bool temperature_humidity_valid;
  bool pressure_valid;
  bool co2_valid;
} airbee_measurements_t;

typedef void (*airbee_sunrise_ready_isr_callback_t)(void);

void airbee_sensors_init(
  airbee_sunrise_ready_isr_callback_t ready_isr_callback);
bool airbee_sensors_start(uint32_t *delay_ms);
bool airbee_sensors_step(uint32_t *delay_ms);
bool airbee_sensors_outdoor_calibration_start(uint32_t *delay_ms);
bool airbee_sensors_outdoor_calibration_step(uint32_t *delay_ms, bool *success);
void airbee_sensors_abort(void);
bool airbee_sensors_busy(void);
bool airbee_sensors_ready_irq_should_wake(void);
const airbee_measurements_t *airbee_sensors_measurements(void);

int16_t airbee_sht45_temperature_centi_c(uint16_t raw_temperature);
uint16_t airbee_sht45_humidity_centi_percent(uint16_t raw_humidity);
