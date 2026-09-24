#include "airbee_bluetooth.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "airbee_sensors.h"
#include "sl_bt_api.h"
#include "sl_status.h"

#define AIRBEE_ADVERTISING_INTERVAL    400U
#define AIRBEE_ADVERTISING_EVENTS      20U
#define AIRBEE_ADVERTISING_HANDLE_NONE 0xFFU
#define AIRBEE_BTHOME_DEVICE_INFO      0x40U

static uint8_t advertising_handle = AIRBEE_ADVERTISING_HANDLE_NONE;
static uint8_t packet_id;
static bool bluetooth_ready;
static bool advertisement_pending;

static void put_u16_le(uint8_t *buffer, size_t *length, uint16_t value)
{
  buffer[(*length)++] = (uint8_t)value;
  buffer[(*length)++] = (uint8_t)(value >> 8);
}

static void put_u24_le(uint8_t *buffer, size_t *length, uint32_t value)
{
  buffer[(*length)++] = (uint8_t)value;
  buffer[(*length)++] = (uint8_t)(value >> 8);
  buffer[(*length)++] = (uint8_t)(value >> 16);
}

static size_t make_advertisement(uint8_t *data)
{
  static const uint8_t name[] = { 'A', 'i', 'r', 'B', 'e', 'e' };
  const airbee_measurements_t *measurement = airbee_sensors_measurements();
  size_t length = 0U;

  data[length++] = 2U;
  data[length++] = 0x01U;
  data[length++] = 0x06U;

  data[length++] = (uint8_t)(sizeof(name) + 1U);
  data[length++] = 0x09U;
  for (size_t i = 0U; i < sizeof(name); ++i) {
    data[length++] = name[i];
  }

  const size_t service_length = length++;
  data[length++] = 0x16U;
  data[length++] = 0xD2U;
  data[length++] = 0xFCU;
  data[length++] = AIRBEE_BTHOME_DEVICE_INFO;

  data[length++] = 0x00U;
  data[length++] = packet_id++;

  if (measurement->temperature_humidity_valid) {
    data[length++] = 0x02U;
    put_u16_le(data,
               &length,
               (uint16_t)airbee_sht45_temperature_centi_c(
                 measurement->sht45_raw_temperature));

    data[length++] = 0x03U;
    put_u16_le(
      data,
      &length,
      airbee_sht45_humidity_centi_percent(measurement->sht45_raw_humidity));
  }

  if (measurement->pressure_valid) {
    const uint32_t pressure_hpa
      = (uint32_t)((measurement->pressure_pa + 50.0f) / 100.0f);
    data[length++] = 0x04U;
    put_u24_le(data, &length, pressure_hpa * 100U);
  }

  if (measurement->co2_valid) {
    data[length++] = 0x12U;
    put_u16_le(data, &length, measurement->co2_ppm);
  }

  data[service_length] = (uint8_t)(length - service_length - 1U);
  return length;
}

static bool ensure_advertiser(void)
{
  if (advertising_handle == AIRBEE_ADVERTISING_HANDLE_NONE
      && sl_bt_advertiser_create_set(&advertising_handle) != SL_STATUS_OK) {
    return false;
  }

  return sl_bt_advertiser_set_timing(advertising_handle,
                                     AIRBEE_ADVERTISING_INTERVAL,
                                     AIRBEE_ADVERTISING_INTERVAL,
                                     0U,
                                     AIRBEE_ADVERTISING_EVENTS)
         == SL_STATUS_OK;
}

void airbee_bluetooth_start(void)
{
  (void)sl_bt_system_start_bluetooth();
}

void airbee_bluetooth_publish(void)
{
  uint8_t data[31];
  const size_t length = make_advertisement(data);

  if (!bluetooth_ready || !ensure_advertiser()) {
    advertisement_pending = true;
    return;
  }

  if (sl_bt_legacy_advertiser_set_data(advertising_handle, 0U, length, data)
        == SL_STATUS_OK
      && sl_bt_legacy_advertiser_start(advertising_handle,
                                       sl_bt_legacy_advertiser_non_connectable)
           == SL_STATUS_OK) {
    advertisement_pending = false;
  } else {
    advertisement_pending = true;
  }
}

void sl_bt_on_event(sl_bt_msg_t *event)
{
  switch (SL_BT_MSG_ID(event->header)) {
    case sl_bt_evt_system_boot_id: {
      int16_t actual_minimum;
      int16_t actual_maximum;
      bluetooth_ready = true;
      (void)sl_bt_system_set_tx_power(
        -30, 100, &actual_minimum, &actual_maximum);
      if (advertising_handle == AIRBEE_ADVERTISING_HANDLE_NONE) {
        (void)sl_bt_advertiser_create_set(&advertising_handle);
      }
      if (advertisement_pending) {
        airbee_bluetooth_publish();
      }
      break;
    }

    default:
      break;
  }
}
