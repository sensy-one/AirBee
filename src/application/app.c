#include "app.h"

#include <stdbool.h>
#include <stdint.h>

#include "airbee_bluetooth.h"
#include "airbee_mode.h"
#include "airbee_sensors.h"
#include "airbee_zigbee.h"
#include "app/framework/include/af.h"

#define AIRBEE_MEASUREMENT_INTERVAL_MS 300000UL
#define AIRBEE_CO2_RETRY_INTERVAL_MS   10000UL

static sl_zigbee_af_event_t measurement_event;
static sl_zigbee_af_event_t sunrise_ready_event;

static bool co2_retry_used;
static bool calibration_prepared;

static void schedule_measurement(void)
{
  sl_zigbee_af_event_set_active(&measurement_event);
}

static void zigbee_connection_changed(bool connected)
{
  if (airbee_mode_is_bluetooth() || airbee_mode_calibration_active()) {
    return;
  }

  if (connected) {
    schedule_measurement();
  } else {
    airbee_sensors_abort();
    sl_zigbee_af_event_set_inactive(&measurement_event);
  }
}

static void sunrise_ready_gpio_isr(void)
{
  sl_zigbee_af_event_set_active(&sunrise_ready_event);
}

static void sunrise_ready_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  if (airbee_sensors_ready_irq_should_wake()) {
    schedule_measurement();
  }
}

static void calibration_event_handler(void)
{
  uint32_t next_delay_ms = 0U;
  if (!calibration_prepared) {
    if (!airbee_sensors_busy()) {
      if (!airbee_sensors_start(&next_delay_ms)) {
        airbee_mode_calibration_complete(false);
        return;
      }
      sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
      return;
    }

    if (!airbee_sensors_step(&next_delay_ms)) {
      sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
      return;
    }

    calibration_prepared = true;
    if (!airbee_sensors_measurements()->co2_valid) {
      airbee_mode_calibration_complete(false);
      return;
    }
    if (!airbee_sensors_outdoor_calibration_start(&next_delay_ms)) {
      airbee_mode_calibration_complete(false);
      return;
    }
    sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
    return;
  }

  bool success = false;
  if (!airbee_sensors_outdoor_calibration_step(&next_delay_ms, &success)) {
    sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
    return;
  }
  airbee_mode_calibration_complete(success);
}

static void measurement_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  if (airbee_mode_calibration_active()) {
    calibration_event_handler();
    return;
  }

  if (!airbee_mode_is_bluetooth() && !airbee_zigbee_connected()) {
    airbee_sensors_abort();
    return;
  }

  uint32_t next_delay_ms = 0U;
  if (!airbee_sensors_busy()) {
    if (!airbee_sensors_start(&next_delay_ms)) {
      sl_zigbee_af_event_set_delay_ms(&measurement_event,
                                      AIRBEE_MEASUREMENT_INTERVAL_MS);
      return;
    }
    sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
    return;
  }

  if (!airbee_sensors_step(&next_delay_ms)) {
    sl_zigbee_af_event_set_delay_ms(&measurement_event, next_delay_ms);
    return;
  }

  if (airbee_mode_is_bluetooth()) {
    airbee_bluetooth_publish();
    if (!airbee_sensors_measurements()->co2_valid && !co2_retry_used) {
      co2_retry_used = true;
      sl_zigbee_af_event_set_delay_ms(&measurement_event,
                                      AIRBEE_CO2_RETRY_INTERVAL_MS);
    } else {
      co2_retry_used = false;
      sl_zigbee_af_event_set_delay_ms(&measurement_event,
                                      AIRBEE_MEASUREMENT_INTERVAL_MS);
    }
  } else {
    airbee_zigbee_publish();
    sl_zigbee_af_event_set_delay_ms(&measurement_event,
                                    AIRBEE_MEASUREMENT_INTERVAL_MS);
  }
}

void app_init(void) {}

void app_process_action(void) {}

void sl_zigbee_af_main_init_cb(void)
{
  sl_zigbee_af_event_init(&measurement_event, measurement_event_handler);
  sl_zigbee_af_isr_event_init(&sunrise_ready_event,
                              sunrise_ready_event_handler);

  airbee_sensors_init(sunrise_ready_gpio_isr);
  airbee_mode_init();
  airbee_zigbee_init(!airbee_mode_is_bluetooth()
                       && !airbee_mode_calibration_active(),
                     zigbee_connection_changed);

  if (airbee_mode_calibration_active()) {
    schedule_measurement();
  } else if (airbee_mode_is_bluetooth()) {
    airbee_bluetooth_start();
    schedule_measurement();
  }
}
