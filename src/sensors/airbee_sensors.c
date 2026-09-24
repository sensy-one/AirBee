#include "airbee_sensors.h"

#include <stddef.h>
#include <string.h>

#include "airbee_board.h"
#include "em_gpio.h"
#include "gpiointerrupt.h"
#include "nvm3.h"
#include "nvm3_default.h"
#include "sl_i2cspm.h"
#include "sl_sleeptimer.h"

#define SHT45_ADDRESS                   0x44U
#define SHT45_MEASURE_LOW_REPEATABILITY 0xE0U
#define SHT45_CONVERSION_DELAY_MS       5U
#define SHT45_MAX_READ_ATTEMPTS         3U

#define BMP581_ADDRESS            0x46U
#define BMP581_REG_CHIP_ID        0x01U
#define BMP581_REG_INT_SOURCE     0x15U
#define BMP581_REG_TEMP_DATA_XLSB 0x1DU
#define BMP581_REG_INT_STATUS     0x27U
#define BMP581_REG_OSR_CONFIG     0x36U
#define BMP581_REG_ODR_CONFIG     0x37U
#define BMP581_DRDY_BIT           0x01U
#define BMP581_CHIP_ID_A          0x50U
#define BMP581_CHIP_ID_B          0x51U
#define BMP581_PRESS_ENABLE       0x40U
#define BMP581_ODR_1_HZ           0x1CU
#define BMP581_ODR_MASK           0x7CU
#define BMP581_ODR_POSITION       2U
#define BMP581_DEEP_DISABLE       0x80U
#define BMP581_POWER_MODE_MASK    0x03U
#define BMP581_POWER_MODE_STANDBY 0x00U
#define BMP581_POWER_MODE_FORCED  0x02U
#define BMP581_POLL_DELAY_MS      5U
#define BMP581_MAX_POLLS          40U

#define SUNRISE_ADDRESS                        0x68U
#define SUNRISE_REG_ERROR_STATUS               0x00U
#define SUNRISE_REG_CALIBRATION_STATUS         0x80U
#define SUNRISE_REG_CALIBRATION_COMMAND        0x82U
#define SUNRISE_REG_MEASUREMENT_MODE           0x95U
#define SUNRISE_REG_ABC_TARGET                 0x9EU
#define SUNRISE_REG_SCR                        0xA3U
#define SUNRISE_REG_METER_CONTROL              0xA5U
#define SUNRISE_REG_SENSOR_STATE               0xC2U
#define SUNRISE_REG_START_MEASUREMENT          0xC3U
#define SUNRISE_REG_PRESSURE                   0xDCU
#define SUNRISE_MODE_SINGLE                    0x01U
#define SUNRISE_START_SINGLE                   0x01U
#define SUNRISE_SCR_RESET                      0xFFU
#define SUNRISE_CALIBRATION_BACKGROUND         0x7C06U
#define SUNRISE_CALIBRATION_BACKGROUND_BIT     0x0020U
#define SUNRISE_ABC_TARGET_PPM                 430U
#define SUNRISE_ABC_DISABLED_BIT               0x02U
#define SUNRISE_PRESSURE_COMP_DISABLED_BIT     0x10U
#define SUNRISE_STARTUP_DELAY_MS               35U
#define SUNRISE_EEPROM_DELAY_MS                120U
#define SUNRISE_RESET_DELAY_MS                 100U
#define SUNRISE_MIN_READY_DELAY_MS             100U
#define SUNRISE_SAMPLE_TIMEOUT_MS              300U
#define SUNRISE_DESIRED_SAMPLES                4U
#define SUNRISE_STATE_WORD_COUNT               15U
#define SUNRISE_STATE_ABC_TIME_INDEX           1U
#define SUNRISE_STATE_PRESSURE_INDEX           13U
#define SUNRISE_READY_MARGIN_MS                800U
#define SUNRISE_CALIBRATION_SAMPLE_INTERVAL_MS 30000UL
#define SUNRISE_CALIBRATION_SAMPLE_COUNT       20U
#define SUNRISE_STATE_NVM_KEY                  0x0000A1BFUL
#define SUNRISE_STATE_NVM_MAGIC                0x41425331UL
#define SUNRISE_STATE_NVM_VERSION              1U
#define SUNRISE_STATE_SAVE_INTERVAL_MS         86400000UL

typedef enum {
  SENSOR_STATE_IDLE,
  SENSOR_STATE_WAIT_ENVIRONMENT,
  SENSOR_STATE_SUNRISE_STARTUP,
  SENSOR_STATE_SUNRISE_CHECK_CONFIG,
  SENSOR_STATE_SUNRISE_EEPROM_WAIT,
  SENSOR_STATE_SUNRISE_RESET_WAIT,
  SENSOR_STATE_SUNRISE_WAIT_MINIMUM,
  SENSOR_STATE_SUNRISE_WAIT_READY,
} sensor_state_t;

typedef enum {
  CALIBRATION_STATE_IDLE,
  CALIBRATION_STATE_STARTUP,
  CALIBRATION_STATE_SAMPLE_START,
  CALIBRATION_STATE_SAMPLE_WAIT_MINIMUM,
  CALIBRATION_STATE_SAMPLE_WAIT_READY,
  CALIBRATION_STATE_COMMAND,
  CALIBRATION_STATE_FINAL_WAIT_MINIMUM,
  CALIBRATION_STATE_FINAL_WAIT_READY,
} calibration_state_t;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t off_time_remainder_seconds;
  uint16_t state[SUNRISE_STATE_WORD_COUNT];
} sunrise_nvm_state_t;

static sensor_state_t state = SENSOR_STATE_IDLE;
static calibration_state_t calibration_state = CALIBRATION_STATE_IDLE;
static airbee_measurements_t measurements;
static bool bmp581_available;
static bool sht45_pending;
static uint8_t sht45_read_attempts;
static bool bmp581_pending;
static uint8_t bmp581_poll_count;
static bool sunrise_config_known;
static bool sunrise_state_valid;
static bool sunrise_state_loaded;
static bool sunrise_is_powered;
static bool sunrise_power_down_tracking;
static uint64_t sunrise_powered_down_tick;
static uint32_t sunrise_off_time_remainder_ms;
static uint32_t sunrise_state_save_elapsed_ms;
static uint8_t calibration_sample_count;
static uint16_t sunrise_state[SUNRISE_STATE_WORD_COUNT];
static airbee_sunrise_ready_isr_callback_t sunrise_ready_callback;

static I2CSPM_Init_TypeDef i2c_init = {
  .port = AIRBEE_I2C_PERIPHERAL,
  .sclPort = AIRBEE_I2C_SCL_PORT,
  .sclPin = AIRBEE_I2C_SCL_PIN,
  .sdaPort = AIRBEE_I2C_SDA_PORT,
  .sdaPin = AIRBEE_I2C_SDA_PIN,
  .i2cRefFreq = 0U,
  .i2cMaxFreq = I2C_FREQ_STANDARD_MAX,
  .i2cClhr = i2cClockHLRStandard,
};

static bool i2c_transfer(uint8_t address,
                         uint16_t flags,
                         uint8_t *write_data,
                         uint16_t write_length,
                         uint8_t *read_data,
                         uint16_t read_length)
{
  I2C_TransferSeq_TypeDef sequence = { 0 };
  sequence.addr = (uint16_t)address << 1;
  sequence.flags = flags;
  if (flags == I2C_FLAG_READ) {
    sequence.buf[0].data = read_data;
    sequence.buf[0].len = read_length;
  } else {
    sequence.buf[0].data = write_data;
    sequence.buf[0].len = write_length;
    sequence.buf[1].data = read_data;
    sequence.buf[1].len = read_length;
  }
  return I2CSPM_Transfer(AIRBEE_I2C_PERIPHERAL, &sequence) == i2cTransferDone;
}

static bool i2c_probe(uint8_t address)
{
  return i2c_transfer(address, I2C_FLAG_WRITE, NULL, 0U, NULL, 0U);
}

static bool i2c_write(uint8_t address, const uint8_t *data, uint8_t length)
{
  return i2c_transfer(
    address, I2C_FLAG_WRITE, (uint8_t *)data, length, NULL, 0U);
}

static bool i2c_read(uint8_t address, uint8_t *data, uint8_t length)
{
  return i2c_transfer(address, I2C_FLAG_READ, NULL, 0U, data, length);
}

static bool
i2c_read_register(uint8_t address, uint8_t reg, uint8_t *data, uint8_t length)
{
  return i2c_transfer(address, I2C_FLAG_WRITE_READ, &reg, 1U, data, length);
}

static bool i2c_write_register(uint8_t address,
                               uint8_t reg,
                               const uint8_t *data,
                               uint8_t length)
{
  uint8_t buffer[32];
  if (length > (sizeof(buffer) - 1U)) {
    return false;
  }

  buffer[0] = reg;
  if (length > 0U) {
    memcpy(&buffer[1], data, length);
  }
  return i2c_write(address, buffer, (uint8_t)(length + 1U));
}

static bool i2c_write_register8(uint8_t address, uint8_t reg, uint8_t value)
{
  return i2c_write_register(address, reg, &value, 1U);
}

static uint16_t read_u16_be(const uint8_t *data)
{
  return ((uint16_t)data[0] << 8) | data[1];
}

static int16_t read_i16_be(const uint8_t *data)
{
  return (int16_t)read_u16_be(data);
}

static uint8_t sht45_crc(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0xFFU;
  for (uint8_t i = 0U; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = ((crc & 0x80U) != 0U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                  : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static bool sht45_start(void)
{
  const uint8_t command = SHT45_MEASURE_LOW_REPEATABILITY;
  return i2c_write(SHT45_ADDRESS, &command, 1U);
}

static bool sht45_read(uint16_t *raw_temperature, uint16_t *raw_humidity)
{
  uint8_t data[6] = { 0 };
  if (!i2c_read(SHT45_ADDRESS, data, sizeof(data))) {
    return false;
  }
  if (sht45_crc(&data[0], 2U) != data[2]
      || sht45_crc(&data[3], 2U) != data[5]) {
    return false;
  }

  *raw_temperature = read_u16_be(&data[0]);
  *raw_humidity = read_u16_be(&data[3]);
  return true;
}

static bool bmp581_init(void)
{
  uint8_t chip_id = 0U;
  uint8_t int_source = 0U;
  if (!i2c_read_register(BMP581_ADDRESS, BMP581_REG_CHIP_ID, &chip_id, 1U)
      || (chip_id != BMP581_CHIP_ID_A && chip_id != BMP581_CHIP_ID_B)
      || !i2c_read_register(
        BMP581_ADDRESS, BMP581_REG_INT_SOURCE, &int_source, 1U)) {
    return false;
  }

  int_source |= BMP581_DRDY_BIT;
  return i2c_write_register8(BMP581_ADDRESS, BMP581_REG_INT_SOURCE, int_source);
}

static bool bmp581_start(void)
{
  const uint8_t osr_config = BMP581_PRESS_ENABLE;
  uint8_t odr_config = 0U;
  uint8_t ignored = 0U;

  if (!i2c_read_register(
        BMP581_ADDRESS, BMP581_REG_ODR_CONFIG, &odr_config, 1U)) {
    return false;
  }

  odr_config &= (uint8_t)~BMP581_DEEP_DISABLE;
  odr_config &= (uint8_t)~BMP581_POWER_MODE_MASK;
  if (!i2c_write_register8(BMP581_ADDRESS,
                           BMP581_REG_ODR_CONFIG,
                           odr_config | BMP581_POWER_MODE_STANDBY)
      || !i2c_write_register8(BMP581_ADDRESS, BMP581_REG_OSR_CONFIG, osr_config)
      || !i2c_read_register(
        BMP581_ADDRESS, BMP581_REG_ODR_CONFIG, &odr_config, 1U)) {
    return false;
  }

  odr_config &= (uint8_t)~BMP581_DEEP_DISABLE;
  odr_config = (uint8_t)((odr_config & (uint8_t)~BMP581_ODR_MASK)
                         | (BMP581_ODR_1_HZ << BMP581_ODR_POSITION));
  odr_config = (uint8_t)((odr_config & (uint8_t)~BMP581_POWER_MODE_MASK)
                         | BMP581_POWER_MODE_FORCED);
  (void)i2c_read_register(BMP581_ADDRESS, BMP581_REG_INT_STATUS, &ignored, 1U);
  return i2c_write_register8(BMP581_ADDRESS, BMP581_REG_ODR_CONFIG, odr_config);
}

static bool bmp581_read_pressure(float *pressure_pa)
{
  uint8_t data[6] = { 0 };
  if (!i2c_read_register(
        BMP581_ADDRESS, BMP581_REG_TEMP_DATA_XLSB, data, sizeof(data))) {
    return false;
  }

  const uint32_t raw_pressure
    = ((uint32_t)data[5] << 16) | ((uint32_t)data[4] << 8) | data[3];
  const float pressure = (float)raw_pressure / 64.0f;
  if (pressure < 30000.0f || pressure > 125000.0f) {
    return false;
  }

  *pressure_pa = pressure;
  return true;
}

static bool bmp581_poll(float *pressure_pa, bool *ready)
{
  uint8_t status = 0U;
  *ready = false;
  if (!i2c_read_register(BMP581_ADDRESS, BMP581_REG_INT_STATUS, &status, 1U)) {
    return false;
  }
  if ((status & BMP581_DRDY_BIT) == 0U) {
    return true;
  }

  *ready = bmp581_read_pressure(pressure_pa);
  return *ready;
}

static uint64_t sleeptimer_ticks_to_ms(uint64_t ticks)
{
  const uint32_t frequency = sl_sleeptimer_get_timer_frequency();
  if (frequency == 0U) {
    return 0U;
  }

  return ((ticks / frequency) * 1000U)
         + (((ticks % frequency) * 1000U) / frequency);
}

static void sunrise_load_state(void)
{
  sunrise_nvm_state_t saved = { 0 };
  if (nvm3_readData(
        nvm3_defaultHandle, SUNRISE_STATE_NVM_KEY, &saved, sizeof(saved))
        != SL_STATUS_OK
      || saved.magic != SUNRISE_STATE_NVM_MAGIC
      || saved.version != SUNRISE_STATE_NVM_VERSION
      || saved.off_time_remainder_seconds >= 3600U) {
    return;
  }

  memcpy(sunrise_state, saved.state, sizeof(sunrise_state));
  sunrise_off_time_remainder_ms
    = (uint32_t)saved.off_time_remainder_seconds * 1000U;
  sunrise_state_valid = true;
  sunrise_state_loaded = true;
  sunrise_powered_down_tick = sl_sleeptimer_get_tick_count64();
  sunrise_power_down_tracking = true;
}

static bool sunrise_store_state(bool force)
{
  if (!sunrise_state_valid
      || (!force && sunrise_state_loaded
          && sunrise_state_save_elapsed_ms < SUNRISE_STATE_SAVE_INTERVAL_MS)) {
    return sunrise_state_valid;
  }

  sunrise_nvm_state_t saved = { 0 };
  saved.magic = SUNRISE_STATE_NVM_MAGIC;
  saved.version = SUNRISE_STATE_NVM_VERSION;
  saved.off_time_remainder_seconds
    = (uint16_t)(sunrise_off_time_remainder_ms / 1000U);
  memcpy(saved.state, sunrise_state, sizeof(saved.state));
  if (nvm3_writeData(
        nvm3_defaultHandle, SUNRISE_STATE_NVM_KEY, &saved, sizeof(saved))
      != SL_STATUS_OK) {
    return false;
  }

  sunrise_state_loaded = true;
  sunrise_state_save_elapsed_ms = 0U;
  return true;
}

static void sunrise_apply_powered_down_time(void)
{
  if (!sunrise_power_down_tracking) {
    return;
  }

  const uint64_t elapsed_ticks
    = sl_sleeptimer_get_tick_count64() - sunrise_powered_down_tick;
  const uint64_t elapsed_ms = sleeptimer_ticks_to_ms(elapsed_ticks);
  sunrise_power_down_tracking = false;

  if (!sunrise_state_valid) {
    sunrise_off_time_remainder_ms = 0U;
    return;
  }

  const uint64_t total_ms
    = (uint64_t)sunrise_off_time_remainder_ms + elapsed_ms;
  const uint64_t elapsed_hours = total_ms / 3600000U;
  sunrise_off_time_remainder_ms = (uint32_t)(total_ms % 3600000U);

  const uint64_t updated_abc_time
    = sunrise_state[SUNRISE_STATE_ABC_TIME_INDEX] + elapsed_hours;
  sunrise_state[SUNRISE_STATE_ABC_TIME_INDEX]
    = updated_abc_time > UINT16_MAX ? UINT16_MAX : (uint16_t)updated_abc_time;

  if (elapsed_ms >= UINT32_MAX
      || UINT32_MAX - sunrise_state_save_elapsed_ms < elapsed_ms) {
    sunrise_state_save_elapsed_ms = UINT32_MAX;
  } else {
    sunrise_state_save_elapsed_ms += (uint32_t)elapsed_ms;
  }
}

static void gpio_write(GPIO_Port_TypeDef port, unsigned int pin, bool high)
{
  if (high) {
    GPIO_PinOutSet(port, pin);
  } else {
    GPIO_PinOutClear(port, pin);
  }
}

static void sunrise_bus_connect(bool connected)
{
  gpio_write(AIRBEE_SUN_SWITCH_PORT,
             AIRBEE_SUN_SWITCH_PIN,
             connected ? AIRBEE_SUN_BUS_CONNECTED
                       : AIRBEE_SUN_BUS_DISCONNECTED);
}

static void sunrise_power(bool enabled)
{
  if (enabled) {
    if (!sunrise_is_powered) {
      sunrise_apply_powered_down_time();
      sunrise_is_powered = true;
    }
    gpio_write(
      AIRBEE_SUN_ENABLE_PORT, AIRBEE_SUN_ENABLE_PIN, AIRBEE_SUN_ENABLED);
  } else {
    sunrise_bus_connect(false);
    gpio_write(
      AIRBEE_SUN_ENABLE_PORT, AIRBEE_SUN_ENABLE_PIN, AIRBEE_SUN_DISABLED);
    if (sunrise_is_powered) {
      sunrise_is_powered = false;
      if (sunrise_state_valid) {
        sunrise_powered_down_tick = sl_sleeptimer_get_tick_count64();
        sunrise_power_down_tracking = true;
      }
    }
  }
}

static void sunrise_wake(void)
{
  (void)i2c_probe(SUNRISE_ADDRESS);
}

static bool sunrise_read_register(uint8_t reg, uint8_t *data, uint8_t length)
{
  sunrise_wake();
  return i2c_read_register(SUNRISE_ADDRESS, reg, data, length);
}

static bool
sunrise_write_register(uint8_t reg, const uint8_t *data, uint8_t length)
{
  sunrise_wake();
  return i2c_write_register(SUNRISE_ADDRESS, reg, data, length);
}

static bool sunrise_write_register8(uint8_t reg, uint8_t value)
{
  return sunrise_write_register(reg, &value, 1U);
}

static bool sunrise_write_register16(uint8_t reg, uint16_t value)
{
  const uint8_t data[2] = {
    (uint8_t)(value >> 8),
    (uint8_t)(value & 0xFFU),
  };
  return sunrise_write_register(reg, data, sizeof(data));
}

static bool sunrise_read_state(void)
{
  uint8_t data[SUNRISE_STATE_WORD_COUNT * 2U] = { 0 };
  if (!sunrise_read_register(SUNRISE_REG_SENSOR_STATE, data, sizeof(data))) {
    return false;
  }

  for (uint8_t i = 0U; i < SUNRISE_STATE_WORD_COUNT; ++i) {
    sunrise_state[i] = read_u16_be(&data[i * 2U]);
  }
  sunrise_state_valid = true;
  return true;
}

static bool sunrise_write_state(void)
{
  uint8_t data[SUNRISE_STATE_WORD_COUNT * 2U] = { 0 };
  for (uint8_t i = 0U; i < SUNRISE_STATE_WORD_COUNT; ++i) {
    data[i * 2U] = (uint8_t)(sunrise_state[i] >> 8);
    data[(i * 2U) + 1U] = (uint8_t)(sunrise_state[i] & 0xFFU);
  }
  return sunrise_write_register(SUNRISE_REG_SENSOR_STATE, data, sizeof(data));
}

static bool sunrise_start_measurement(void)
{
  bool started = false;
  if (sunrise_state_valid) {
    sunrise_state[0] = 1U;
    if (measurements.pressure_valid) {
      const float pressure_tenths_hpa = measurements.pressure_pa / 10.0f;
      sunrise_state[SUNRISE_STATE_PRESSURE_INDEX]
        = (pressure_tenths_hpa >= 65535.0f)
            ? UINT16_MAX
            : (uint16_t)(pressure_tenths_hpa + 0.5f);
    }
    started = sunrise_write_state();
  } else {
    bool pressure_ready = true;
    if (measurements.pressure_valid) {
      const float pressure_tenths_hpa = measurements.pressure_pa / 10.0f;
      const uint16_t pressure = pressure_tenths_hpa >= 65535.0f
                                  ? UINT16_MAX
                                  : (uint16_t)(pressure_tenths_hpa + 0.5f);
      pressure_ready = sunrise_write_register16(SUNRISE_REG_PRESSURE, pressure);
    }
    started = pressure_ready
              && sunrise_write_register8(SUNRISE_REG_START_MEASUREMENT,
                                         SUNRISE_START_SINGLE);
  }
  return started;
}

static bool sunrise_read_measurement(uint16_t *co2_ppm)
{
  uint8_t data[10] = { 0 };
  if (!sunrise_read_register(SUNRISE_REG_ERROR_STATUS, data, sizeof(data))) {
    return false;
  }

  const uint16_t error_status = read_u16_be(&data[0]);
  const int16_t co2 = read_i16_be(&data[6]);
  if (sunrise_read_state()) {
    (void)sunrise_store_state(false);
  }
  if (error_status != 0U || co2 < 0) {
    return false;
  }

  *co2_ppm = (uint16_t)co2;
  return true;
}

static bool sunrise_start_background_calibration(void)
{
  return sunrise_write_register16(SUNRISE_REG_CALIBRATION_STATUS, 0U)
         && sunrise_write_register16(SUNRISE_REG_CALIBRATION_COMMAND,
                                     SUNRISE_CALIBRATION_BACKGROUND);
}

static bool sunrise_background_calibration_succeeded(void)
{
  uint8_t data[2] = { 0 };
  return sunrise_read_register(
           SUNRISE_REG_CALIBRATION_STATUS, data, sizeof(data))
         && (read_u16_be(data) & SUNRISE_CALIBRATION_BACKGROUND_BIT) != 0U;
}

static void finish_measurement(void)
{
  sunrise_power(false);
  state = SENSOR_STATE_IDLE;
}

static void sunrise_gpio_callback(uint8_t interrupt_number, void *context)
{
  (void)interrupt_number;
  (void)context;
  if (sunrise_ready_callback != NULL) {
    sunrise_ready_callback();
  }
}

void airbee_sensors_init(airbee_sunrise_ready_isr_callback_t ready_isr_callback)
{
  sunrise_ready_callback = ready_isr_callback;

  GPIO_PinModeSet(AIRBEE_SUN_SWITCH_PORT,
                  AIRBEE_SUN_SWITCH_PIN,
                  gpioModePushPull,
                  AIRBEE_SUN_BUS_DISCONNECTED);
  GPIO_PinModeSet(AIRBEE_SUN_ENABLE_PORT,
                  AIRBEE_SUN_ENABLE_PIN,
                  gpioModePushPull,
                  AIRBEE_SUN_DISABLED);
  GPIO_PinModeSet(
    AIRBEE_SUN_NRDY_PORT, AIRBEE_SUN_NRDY_PIN, gpioModeInputPullFilter, 1U);

  I2CSPM_Init(&i2c_init);
  sunrise_load_state();

  const unsigned int interrupt_number = GPIOINT_CallbackRegisterExt(
    AIRBEE_SUN_NRDY_PIN, sunrise_gpio_callback, NULL);
  if (interrupt_number != INTERRUPT_UNAVAILABLE) {
    GPIO_ExtIntConfig(AIRBEE_SUN_NRDY_PORT,
                      AIRBEE_SUN_NRDY_PIN,
                      interrupt_number,
                      false,
                      true,
                      true);
  }

  sunrise_power(false);
  bmp581_available = bmp581_init();
}

bool airbee_sensors_start(uint32_t *delay_ms)
{
  if (delay_ms == NULL || state != SENSOR_STATE_IDLE
      || calibration_state != CALIBRATION_STATE_IDLE) {
    return false;
  }

  memset(&measurements, 0, sizeof(measurements));
  sht45_pending = sht45_start();
  sht45_read_attempts = 0U;

  if (!bmp581_available) {
    bmp581_available = bmp581_init();
  }
  bmp581_pending = bmp581_available && bmp581_start();
  if (!bmp581_pending) {
    bmp581_available = false;
  }
  bmp581_poll_count = 0U;
  state = SENSOR_STATE_WAIT_ENVIRONMENT;
  *delay_ms = SHT45_CONVERSION_DELAY_MS;
  return true;
}

bool airbee_sensors_step(uint32_t *delay_ms)
{
  if (delay_ms == NULL || state == SENSOR_STATE_IDLE) {
    return true;
  }

  for (;;) {
    switch (state) {
      case SENSOR_STATE_WAIT_ENVIRONMENT: {
        if (sht45_pending) {
          uint16_t raw_temperature = 0U;
          uint16_t raw_humidity = 0U;
          if (sht45_read(&raw_temperature, &raw_humidity)) {
            measurements.sht45_raw_temperature = raw_temperature;
            measurements.sht45_raw_humidity = raw_humidity;
            measurements.temperature_humidity_valid = true;
            sht45_pending = false;
          } else {
            sht45_pending = ++sht45_read_attempts < SHT45_MAX_READ_ATTEMPTS
                            && sht45_start();
          }
        }

        if (bmp581_pending) {
          bool ready = false;
          float pressure_pa = 0.0f;
          if (!bmp581_poll(&pressure_pa, &ready)) {
            bmp581_pending = false;
            bmp581_available = false;
          } else if (ready) {
            measurements.pressure_pa = pressure_pa;
            measurements.pressure_valid = true;
            bmp581_pending = false;
            bmp581_available = true;
          } else if (++bmp581_poll_count >= BMP581_MAX_POLLS) {
            if (bmp581_read_pressure(&pressure_pa)) {
              measurements.pressure_pa = pressure_pa;
              measurements.pressure_valid = true;
            }
            bmp581_pending = false;
          }
        }

        if (sht45_pending || bmp581_pending) {
          *delay_ms = BMP581_POLL_DELAY_MS;
          return false;
        }

        sunrise_power(true);
        state = SENSOR_STATE_SUNRISE_STARTUP;
        *delay_ms = SUNRISE_STARTUP_DELAY_MS;
        return false;
      }

      case SENSOR_STATE_SUNRISE_STARTUP:
        sunrise_bus_connect(true);
        state = SENSOR_STATE_SUNRISE_CHECK_CONFIG;
        continue;

      case SENSOR_STATE_SUNRISE_CHECK_CONFIG: {
        if (!sunrise_config_known) {
          uint8_t measurement_config[5] = { 0 };
          uint8_t abc_target_data[2] = { 0 };
          uint8_t meter_control = 0U;
          if (!sunrise_read_register(SUNRISE_REG_MEASUREMENT_MODE,
                                     measurement_config,
                                     sizeof(measurement_config))
              || !sunrise_read_register(SUNRISE_REG_ABC_TARGET,
                                        abc_target_data,
                                        sizeof(abc_target_data))
              || !sunrise_read_register(
                SUNRISE_REG_METER_CONTROL, &meter_control, 1U)) {
            finish_measurement();
            return true;
          }

          const uint16_t configured_samples
            = read_u16_be(&measurement_config[3]);
          if (measurement_config[0] != SUNRISE_MODE_SINGLE
              || configured_samples != SUNRISE_DESIRED_SAMPLES) {
            measurement_config[0] = SUNRISE_MODE_SINGLE;
            measurement_config[3] = (uint8_t)(SUNRISE_DESIRED_SAMPLES >> 8);
            measurement_config[4] = (uint8_t)(SUNRISE_DESIRED_SAMPLES & 0xFFU);
            if (!sunrise_write_register(SUNRISE_REG_MEASUREMENT_MODE,
                                        measurement_config,
                                        sizeof(measurement_config))) {
              finish_measurement();
              return true;
            }
            state = SENSOR_STATE_SUNRISE_EEPROM_WAIT;
            *delay_ms = SUNRISE_EEPROM_DELAY_MS;
            return false;
          }

          if (read_u16_be(abc_target_data) != SUNRISE_ABC_TARGET_PPM) {
            if (!sunrise_write_register16(SUNRISE_REG_ABC_TARGET,
                                          SUNRISE_ABC_TARGET_PPM)) {
              finish_measurement();
              return true;
            }
            state = SENSOR_STATE_SUNRISE_EEPROM_WAIT;
            *delay_ms = SUNRISE_EEPROM_DELAY_MS;
            return false;
          }

          const uint8_t disabled_features
            = SUNRISE_ABC_DISABLED_BIT | SUNRISE_PRESSURE_COMP_DISABLED_BIT;
          if ((meter_control & disabled_features) != 0U) {
            meter_control &= (uint8_t)~disabled_features;
            if (!sunrise_write_register8(SUNRISE_REG_METER_CONTROL,
                                         meter_control)) {
              finish_measurement();
              return true;
            }
            state = SENSOR_STATE_SUNRISE_EEPROM_WAIT;
            *delay_ms = SUNRISE_EEPROM_DELAY_MS;
            return false;
          }
          sunrise_config_known = true;
        }

        if (!sunrise_start_measurement()) {
          finish_measurement();
          return true;
        }
        state = SENSOR_STATE_SUNRISE_WAIT_MINIMUM;
        *delay_ms = SUNRISE_MIN_READY_DELAY_MS;
        return false;
      }

      case SENSOR_STATE_SUNRISE_EEPROM_WAIT:
        if (!sunrise_write_register8(SUNRISE_REG_SCR, SUNRISE_SCR_RESET)) {
          finish_measurement();
          return true;
        }
        sunrise_state_valid = false;
        sunrise_state_loaded = false;
        sunrise_config_known = false;
        state = SENSOR_STATE_SUNRISE_RESET_WAIT;
        *delay_ms = SUNRISE_RESET_DELAY_MS;
        return false;

      case SENSOR_STATE_SUNRISE_RESET_WAIT:
        state = SENSOR_STATE_SUNRISE_CHECK_CONFIG;
        continue;

      case SENSOR_STATE_SUNRISE_WAIT_MINIMUM:
        if (GPIO_PinInGet(AIRBEE_SUN_NRDY_PORT, AIRBEE_SUN_NRDY_PIN) == 0) {
          uint16_t co2_ppm = 0U;
          measurements.co2_valid = sunrise_read_measurement(&co2_ppm);
          measurements.co2_ppm = co2_ppm;
          finish_measurement();
          return true;
        }
        state = SENSOR_STATE_SUNRISE_WAIT_READY;
        *delay_ms
          = ((uint32_t)SUNRISE_DESIRED_SAMPLES * SUNRISE_SAMPLE_TIMEOUT_MS)
            + SUNRISE_READY_MARGIN_MS - SUNRISE_MIN_READY_DELAY_MS;
        return false;

      case SENSOR_STATE_SUNRISE_WAIT_READY: {
        uint16_t co2_ppm = 0U;
        measurements.co2_valid = sunrise_read_measurement(&co2_ppm);
        measurements.co2_ppm = co2_ppm;
        finish_measurement();
        return true;
      }

      case SENSOR_STATE_IDLE:
      default:
        return true;
    }
  }
}

bool airbee_sensors_outdoor_calibration_start(uint32_t *delay_ms)
{
  if (delay_ms == NULL || state != SENSOR_STATE_IDLE
      || calibration_state != CALIBRATION_STATE_IDLE || !sunrise_config_known) {
    return false;
  }

  sunrise_power(true);
  calibration_sample_count = 0U;
  calibration_state = CALIBRATION_STATE_STARTUP;
  *delay_ms = SUNRISE_STARTUP_DELAY_MS;
  return true;
}

bool airbee_sensors_outdoor_calibration_step(uint32_t *delay_ms, bool *success)
{
  if (delay_ms == NULL || success == NULL
      || calibration_state == CALIBRATION_STATE_IDLE) {
    return false;
  }

  *success = false;
  switch (calibration_state) {
    case CALIBRATION_STATE_STARTUP:
      sunrise_bus_connect(true);
      if (!sunrise_start_measurement()) {
        sunrise_power(false);
        calibration_state = CALIBRATION_STATE_IDLE;
        return true;
      }
      calibration_state = CALIBRATION_STATE_SAMPLE_WAIT_MINIMUM;
      *delay_ms = SUNRISE_MIN_READY_DELAY_MS;
      return false;

    case CALIBRATION_STATE_SAMPLE_START:
      if (!sunrise_start_measurement()) {
        sunrise_power(false);
        calibration_state = CALIBRATION_STATE_IDLE;
        return true;
      }
      calibration_state = CALIBRATION_STATE_SAMPLE_WAIT_MINIMUM;
      *delay_ms = SUNRISE_MIN_READY_DELAY_MS;
      return false;

    case CALIBRATION_STATE_SAMPLE_WAIT_MINIMUM:
      if (GPIO_PinInGet(AIRBEE_SUN_NRDY_PORT, AIRBEE_SUN_NRDY_PIN) != 0) {
        calibration_state = CALIBRATION_STATE_SAMPLE_WAIT_READY;
        *delay_ms
          = ((uint32_t)SUNRISE_DESIRED_SAMPLES * SUNRISE_SAMPLE_TIMEOUT_MS)
            + SUNRISE_READY_MARGIN_MS - SUNRISE_MIN_READY_DELAY_MS;
        return false;
      }
      break;

    case CALIBRATION_STATE_SAMPLE_WAIT_READY:
      break;

    case CALIBRATION_STATE_COMMAND:
      if (!sunrise_start_background_calibration()
          || !sunrise_start_measurement()) {
        sunrise_power(false);
        calibration_state = CALIBRATION_STATE_IDLE;
        return true;
      }
      calibration_state = CALIBRATION_STATE_FINAL_WAIT_MINIMUM;
      *delay_ms = SUNRISE_MIN_READY_DELAY_MS;
      return false;

    case CALIBRATION_STATE_FINAL_WAIT_MINIMUM:
      if (GPIO_PinInGet(AIRBEE_SUN_NRDY_PORT, AIRBEE_SUN_NRDY_PIN) != 0) {
        calibration_state = CALIBRATION_STATE_FINAL_WAIT_READY;
        *delay_ms
          = ((uint32_t)SUNRISE_DESIRED_SAMPLES * SUNRISE_SAMPLE_TIMEOUT_MS)
            + SUNRISE_READY_MARGIN_MS - SUNRISE_MIN_READY_DELAY_MS;
        return false;
      }
      break;

    case CALIBRATION_STATE_FINAL_WAIT_READY:
      break;

    case CALIBRATION_STATE_IDLE:
    default:
      return false;
  }

  uint16_t co2_ppm = 0U;
  if (calibration_state == CALIBRATION_STATE_SAMPLE_WAIT_MINIMUM
      || calibration_state == CALIBRATION_STATE_SAMPLE_WAIT_READY) {
    if (!sunrise_read_measurement(&co2_ppm)) {
      sunrise_power(false);
      calibration_state = CALIBRATION_STATE_IDLE;
      return true;
    }

    calibration_sample_count++;
    calibration_state
      = calibration_sample_count >= SUNRISE_CALIBRATION_SAMPLE_COUNT
          ? CALIBRATION_STATE_COMMAND
          : CALIBRATION_STATE_SAMPLE_START;
    *delay_ms = SUNRISE_CALIBRATION_SAMPLE_INTERVAL_MS;
    return false;
  }

  const bool measurement_ok = sunrise_read_measurement(&co2_ppm);
  const bool calibration_ok = sunrise_background_calibration_succeeded();
  const bool state_saved = sunrise_store_state(true);
  sunrise_power(false);
  calibration_state = CALIBRATION_STATE_IDLE;
  *success = measurement_ok && calibration_ok && state_saved;
  return true;
}

void airbee_sensors_abort(void)
{
  sunrise_power(false);
  sht45_pending = false;
  bmp581_pending = false;
  state = SENSOR_STATE_IDLE;
  calibration_state = CALIBRATION_STATE_IDLE;
}

bool airbee_sensors_busy(void)
{
  return state != SENSOR_STATE_IDLE;
}

bool airbee_sensors_ready_irq_should_wake(void)
{
  return state == SENSOR_STATE_SUNRISE_WAIT_READY
         || calibration_state == CALIBRATION_STATE_SAMPLE_WAIT_READY
         || calibration_state == CALIBRATION_STATE_FINAL_WAIT_READY;
}

const airbee_measurements_t *airbee_sensors_measurements(void)
{
  return &measurements;
}

int16_t airbee_sht45_temperature_centi_c(uint16_t raw_temperature)
{
  const int32_t converted
    = -4500L
      + (int32_t)(((uint32_t)17500U * raw_temperature + 32767U) / 65535U);
  return (int16_t)converted;
}

uint16_t airbee_sht45_humidity_centi_percent(uint16_t raw_humidity)
{
  int32_t converted
    = -600L + (int32_t)(((uint32_t)12500U * raw_humidity + 32767U) / 65535U);
  if (converted < 0L) {
    converted = 0L;
  } else if (converted > 10000L) {
    converted = 10000L;
  }
  return (uint16_t)converted;
}
