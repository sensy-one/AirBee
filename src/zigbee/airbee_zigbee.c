#include "airbee_zigbee.h"

#include <stddef.h>
#include <stdint.h>

#include "airbee_sensors.h"
#include "app/framework/include/af.h"
#include "app/framework/plugin/end-device-support/end-device-support.h"
#include "app/framework/util/attribute-table.h"
#include "network-steering.h"

#define AIRBEE_ENDPOINT               1U
#define AIRBEE_COORDINATOR_ENDPOINT   1U
#define AIRBEE_COORDINATOR_NODE_ID    0x0000U
#define AIRBEE_JOIN_RETRY_INITIAL_MS  300000UL
#define AIRBEE_JOIN_RETRY_MAX_MS      1800000UL
#define AIRBEE_COMMISSIONING_AWAKE_MS 60000UL
#define AIRBEE_REPORT_GAP_MS          20UL
#define AIRBEE_RADIO_TX_POWER_DBM     10
#define AIRBEE_DISABLED_POLL_MS       0x7FFFFFFFUL

#define AIRBEE_TEMP_CLUSTER_ID          0x0402U
#define AIRBEE_PRESSURE_CLUSTER_ID      0x0403U
#define AIRBEE_HUMIDITY_CLUSTER_ID      0x0405U
#define AIRBEE_CO2_CLUSTER_ID           0x040DU
#define AIRBEE_MEASURED_VALUE_ATTRIBUTE 0x0000U
#define AIRBEE_MINIMUM_VALUE_ATTRIBUTE  0x0001U
#define AIRBEE_MAXIMUM_VALUE_ATTRIBUTE  0x0002U
#define AIRBEE_REPORT_CLUSTER_COUNT     4U
#define AIRBEE_REPORT_BUFFER_SIZE       32U

static sl_zigbee_af_event_t join_event;
static sl_zigbee_af_event_t report_event;
static sl_zigbee_af_event_t commissioning_awake_event;

static airbee_zigbee_connection_callback_t connection_callback;
static bool zigbee_enabled;
static bool commissioning_active;
static uint32_t join_retry_ms = AIRBEE_JOIN_RETRY_INITIAL_MS;
static uint8_t report_index;
static bool report_in_flight;

bool airbee_zigbee_connected(void)
{
  return zigbee_enabled
         && sl_zigbee_af_network_state() == SL_ZIGBEE_JOINED_NETWORK;
}

static void disable_zigbee_activity(void)
{
  sl_zigbee_af_set_default_poll_control_cb(SL_ZIGBEE_AF_LONG_POLL);
  sl_zigbee_af_set_long_poll_interval_ms_cb(AIRBEE_DISABLED_POLL_MS);
  sl_zigbee_af_remove_from_current_app_tasks_cb(UINT32_MAX);
}

static void schedule_join_retry(void)
{
  sl_zigbee_af_event_set_delay_ms(&join_event, join_retry_ms);
  if (join_retry_ms < (AIRBEE_JOIN_RETRY_MAX_MS / 2UL)) {
    join_retry_ms *= 2UL;
  } else {
    join_retry_ms = AIRBEE_JOIN_RETRY_MAX_MS;
  }
}

static void join_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  if (!zigbee_enabled || airbee_zigbee_connected() || commissioning_active) {
    return;
  }

  if (sl_zigbee_af_network_state() == SL_ZIGBEE_JOINED_NETWORK_NO_PARENT) {
    (void)sl_zigbee_af_start_move_cb();
    return;
  }

  const sl_status_t status = sl_zigbee_af_network_steering_start();
  if (status == SL_STATUS_OK) {
    commissioning_active = true;
  } else {
    schedule_join_retry();
  }
}

static void commissioning_awake_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  sl_zigbee_af_set_default_poll_control_cb(SL_ZIGBEE_AF_LONG_POLL);
}

static void write_attribute(uint16_t cluster_id,
                            uint16_t attribute_id,
                            uint8_t data_type,
                            const void *value)
{
  (void)sl_zigbee_af_write_server_attribute(
    AIRBEE_ENDPOINT, cluster_id, attribute_id, (uint8_t *)value, data_type);
}

static void initialize_co2_attributes(void)
{
  const float initial_fraction = 0.0f;
  const float minimum_fraction = 0.0f;
  const float maximum_fraction = 0.01f;

  write_attribute(AIRBEE_CO2_CLUSTER_ID,
                  AIRBEE_MEASURED_VALUE_ATTRIBUTE,
                  ZCL_FLOAT_SINGLE_ATTRIBUTE_TYPE,
                  &initial_fraction);
  write_attribute(AIRBEE_CO2_CLUSTER_ID,
                  AIRBEE_MINIMUM_VALUE_ATTRIBUTE,
                  ZCL_FLOAT_SINGLE_ATTRIBUTE_TYPE,
                  &minimum_fraction);
  write_attribute(AIRBEE_CO2_CLUSTER_ID,
                  AIRBEE_MAXIMUM_VALUE_ATTRIBUTE,
                  ZCL_FLOAT_SINGLE_ATTRIBUTE_TYPE,
                  &maximum_fraction);
}

static void update_measurement_attributes(void)
{
  const airbee_measurements_t *measurement = airbee_sensors_measurements();

  if (measurement->temperature_humidity_valid) {
    const int16_t temperature
      = airbee_sht45_temperature_centi_c(measurement->sht45_raw_temperature);
    const uint16_t humidity
      = airbee_sht45_humidity_centi_percent(measurement->sht45_raw_humidity);
    write_attribute(AIRBEE_TEMP_CLUSTER_ID,
                    AIRBEE_MEASURED_VALUE_ATTRIBUTE,
                    ZCL_INT16S_ATTRIBUTE_TYPE,
                    &temperature);
    write_attribute(AIRBEE_HUMIDITY_CLUSTER_ID,
                    AIRBEE_MEASURED_VALUE_ATTRIBUTE,
                    ZCL_INT16U_ATTRIBUTE_TYPE,
                    &humidity);
  }

  if (measurement->pressure_valid) {
    const int16_t pressure_hpa
      = (int16_t)((measurement->pressure_pa + 50.0f) / 100.0f);
    write_attribute(AIRBEE_PRESSURE_CLUSTER_ID,
                    AIRBEE_MEASURED_VALUE_ATTRIBUTE,
                    ZCL_INT16S_ATTRIBUTE_TYPE,
                    &pressure_hpa);
  }

  if (measurement->co2_valid) {
    const float co2_fraction = (float)measurement->co2_ppm / 1000000.0f;
    write_attribute(AIRBEE_CO2_CLUSTER_ID,
                    AIRBEE_MEASURED_VALUE_ATTRIBUTE,
                    ZCL_FLOAT_SINGLE_ATTRIBUTE_TYPE,
                    &co2_fraction);
  }
}

static bool report_cluster_is_valid(uint8_t index)
{
  const airbee_measurements_t *measurement = airbee_sensors_measurements();
  switch (index) {
    case 0U:
    case 1U:
      return measurement->temperature_humidity_valid;
    case 2U:
      return measurement->pressure_valid;
    case 3U:
      return measurement->co2_valid;
    default:
      return false;
  }
}

static uint16_t report_cluster_id(uint8_t index)
{
  static const uint16_t cluster_ids[AIRBEE_REPORT_CLUSTER_COUNT] = {
    AIRBEE_TEMP_CLUSTER_ID,
    AIRBEE_HUMIDITY_CLUSTER_ID,
    AIRBEE_PRESSURE_CLUSTER_ID,
    AIRBEE_CO2_CLUSTER_ID,
  };
  return cluster_ids[index];
}

static void report_sent_callback(sl_zigbee_outgoing_message_type_t type,
                                 uint16_t index_or_destination,
                                 sl_zigbee_aps_frame_t *aps_frame,
                                 uint16_t message_length,
                                 uint8_t *message,
                                 sl_status_t status)
{
  (void)type;
  (void)index_or_destination;
  (void)aps_frame;
  (void)message_length;
  (void)message;
  (void)status;

  if (report_in_flight) {
    report_in_flight = false;
    ++report_index;
    sl_zigbee_af_event_set_delay_ms(&report_event, AIRBEE_REPORT_GAP_MS);
  }
}

static void report_event_handler(sl_zigbee_af_event_t *event)
{
  (void)event;
  if (!airbee_zigbee_connected() || report_in_flight) {
    return;
  }

  while (report_index < AIRBEE_REPORT_CLUSTER_COUNT) {
    uint8_t report_buffer[AIRBEE_REPORT_BUFFER_SIZE] = { 0 };
    uint8_t report_length = 0U;
    const uint16_t cluster_id = report_cluster_id(report_index);
    if (!report_cluster_is_valid(report_index)
        || sl_zigbee_af_append_attribute_report_fields(
             AIRBEE_ENDPOINT,
             cluster_id,
             AIRBEE_MEASURED_VALUE_ATTRIBUTE,
             CLUSTER_MASK_SERVER,
             report_buffer,
             sizeof(report_buffer),
             &report_length)
             != SL_ZIGBEE_ZCL_STATUS_SUCCESS) {
      ++report_index;
      continue;
    }

    sl_zigbee_af_fill_command_global_server_to_client_report_attributes(
      cluster_id, report_buffer, report_length);
    sl_zigbee_af_set_command_endpoints(AIRBEE_ENDPOINT,
                                       AIRBEE_COORDINATOR_ENDPOINT);
    const sl_status_t status
      = sl_zigbee_af_send_command_unicast_with_cb(SL_ZIGBEE_OUTGOING_DIRECT,
                                                  AIRBEE_COORDINATOR_NODE_ID,
                                                  report_sent_callback);
    if (status == SL_STATUS_OK) {
      report_in_flight = true;
      return;
    }
    ++report_index;
  }
}

void airbee_zigbee_init(bool enabled,
                        airbee_zigbee_connection_callback_t callback)
{
  zigbee_enabled = enabled;
  connection_callback = callback;

  sl_zigbee_af_event_init(&join_event, join_event_handler);
  sl_zigbee_af_event_init(&report_event, report_event_handler);
  sl_zigbee_af_event_init(&commissioning_awake_event,
                          commissioning_awake_event_handler);
  initialize_co2_attributes();

  if (!zigbee_enabled) {
    disable_zigbee_activity();
  } else if (airbee_zigbee_connected()) {
    if (connection_callback != NULL) {
      connection_callback(true);
    }
  } else {
    sl_zigbee_af_event_set_delay_ms(&join_event, 1000UL);
  }
}

void airbee_zigbee_publish(void)
{
  if (!airbee_zigbee_connected()) {
    return;
  }

  update_measurement_attributes();
  report_index = 0U;
  report_in_flight = false;
  sl_zigbee_af_event_set_active(&report_event);
}

void sl_zigbee_af_stack_status_cb(sl_status_t status)
{
  if (!zigbee_enabled) {
    disable_zigbee_activity();
    return;
  }

  if (status == SL_STATUS_NETWORK_UP) {
    commissioning_active = false;
    join_retry_ms = AIRBEE_JOIN_RETRY_INITIAL_MS;
    sl_zigbee_af_event_set_inactive(&join_event);
    (void)sl_zigbee_set_radio_power(AIRBEE_RADIO_TX_POWER_DBM);
    sl_zigbee_af_set_default_poll_control_cb(SL_ZIGBEE_AF_SHORT_POLL);
    sl_zigbee_af_event_set_delay_ms(&commissioning_awake_event,
                                    AIRBEE_COMMISSIONING_AWAKE_MS);
    if (connection_callback != NULL) {
      connection_callback(true);
    }
  } else if (status == SL_STATUS_NETWORK_DOWN) {
    report_in_flight = false;
    sl_zigbee_af_event_set_inactive(&report_event);
    sl_zigbee_af_event_set_inactive(&commissioning_awake_event);
    sl_zigbee_af_set_default_poll_control_cb(SL_ZIGBEE_AF_LONG_POLL);
    if (connection_callback != NULL) {
      connection_callback(false);
    }
    if (!commissioning_active
        && !sl_zigbee_af_event_is_scheduled(&join_event)) {
      sl_zigbee_af_event_set_delay_ms(&join_event, 1000UL);
    }
  }
}

void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t total_beacons,
                                               uint8_t join_attempts,
                                               uint8_t final_state)
{
  (void)total_beacons;
  (void)join_attempts;
  (void)final_state;
  if (!zigbee_enabled) {
    return;
  }
  commissioning_active = false;
  if (status != SL_STATUS_OK) {
    schedule_join_retry();
  }
}

#ifndef SL_CATALOG_ZIGBEE_EZSP_PRESENT
void sl_zigbee_af_radio_needs_calibrating_cb(void)
{
  sl_mac_calibrate_current_channel();
}
#endif
