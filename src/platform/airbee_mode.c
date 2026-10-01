#include "airbee_mode.h"

#include <stddef.h>
#include <stdint.h>

#include "airbee_board.h"
#include "airbee_zigbee.h"
#include "cmsis_os2.h"
#include "em_gpio.h"
#include "gpiointerrupt.h"
#include "nvm3.h"
#include "nvm3_default.h"
#include "sl_hal_gpio.h"
#include "sl_apploader_util.h"
#include "sl_power_manager.h"
#include "sl_sleeptimer.h"

#define AIRBEE_MODE_HOLD_MS             5000UL
#define AIRBEE_DFU_HOLD_MS              12000UL
#define AIRBEE_RESET_HOLD_MS            20000UL
#define AIRBEE_RESET_BLINK_MS           100UL
#define AIRBEE_BUTTON_DEBOUNCE_MS       30UL
#define AIRBEE_BUTTON_POLL_MS           50UL
#define AIRBEE_MODE_BLINK_COUNT         4U
#define AIRBEE_MODE_BLINK_ON_MS         160UL
#define AIRBEE_MODE_BLINK_OFF_MS        120UL
#define AIRBEE_STARTUP_BUTTON_SAMPLE_MS 50U
#define AIRBEE_STARTUP_BUTTON_SAMPLES   4U
#define AIRBEE_MODE_NVM_KEY             0x0000A1BEUL
#define AIRBEE_MODE_WRITE_ATTEMPTS      3U

typedef enum {
  AIRBEE_MODE_ZIGBEE = 0U,
  AIRBEE_MODE_BLUETOOTH = 1U,
  AIRBEE_MODE_RESET_PENDING = 2U,
} airbee_mode_t;

static airbee_mode_t active_mode = AIRBEE_MODE_ZIGBEE;
static bool reset_pending;
static osThreadId_t button_thread_id;
static uint32_t button_wake_mask;
static bool calibration_active;
static volatile int8_t calibration_result;

static uint32_t milliseconds_to_ticks(uint32_t milliseconds)
{
  const uint32_t frequency = osKernelGetTickFreq();
  const uint32_t ticks
    = (uint32_t)(((uint64_t)milliseconds * frequency + 999U) / 1000U);
  return ticks == 0U ? 1U : ticks;
}

static void leds_off(void)
{
  GPIO_PinOutSet(AIRBEE_LED_RED_PORT, AIRBEE_LED_RED_PIN);
  GPIO_PinOutSet(AIRBEE_LED_GREEN_PORT, AIRBEE_LED_GREEN_PIN);
  GPIO_PinOutSet(AIRBEE_LED_BLUE_PORT, AIRBEE_LED_BLUE_PIN);
}

static void bluetooth_led_on(void)
{
  leds_off();
  GPIO_PinOutClear(AIRBEE_LED_BLUE_PORT, AIRBEE_LED_BLUE_PIN);
}

static void dfu_led_on(void)
{
  leds_off();
  GPIO_PinOutClear(AIRBEE_LED_GREEN_PORT, AIRBEE_LED_GREEN_PIN);
}

static void zigbee_led_on(void)
{
  leds_off();
  GPIO_PinOutClear(AIRBEE_LED_RED_PORT, AIRBEE_LED_RED_PIN);
}

static void blink_mode(airbee_mode_t mode)
{
  for (uint8_t blink = 0U; blink < AIRBEE_MODE_BLINK_COUNT; blink++) {
    if (mode == AIRBEE_MODE_BLUETOOTH) {
      bluetooth_led_on();
    } else {
      zigbee_led_on();
    }
    (void)osDelay(milliseconds_to_ticks(AIRBEE_MODE_BLINK_ON_MS));
    leds_off();
    if ((blink + 1U) < AIRBEE_MODE_BLINK_COUNT) {
      (void)osDelay(milliseconds_to_ticks(AIRBEE_MODE_BLINK_OFF_MS));
    }
  }
}

static void blink_dfu(void)
{
  for (uint8_t blink = 0U; blink < AIRBEE_MODE_BLINK_COUNT; blink++) {
    dfu_led_on();
    (void)osDelay(milliseconds_to_ticks(AIRBEE_MODE_BLINK_ON_MS));
    leds_off();
    if ((blink + 1U) < AIRBEE_MODE_BLINK_COUNT) {
      (void)osDelay(milliseconds_to_ticks(AIRBEE_MODE_BLINK_OFF_MS));
    }
  }
}

static void load_mode(void)
{
  uint8_t stored_mode = AIRBEE_MODE_ZIGBEE;
  if (nvm3_readData(nvm3_defaultHandle,
                    AIRBEE_MODE_NVM_KEY,
                    &stored_mode,
                    sizeof(stored_mode))
        == SL_STATUS_OK) {
    active_mode = stored_mode == AIRBEE_MODE_BLUETOOTH
                    ? AIRBEE_MODE_BLUETOOTH : AIRBEE_MODE_ZIGBEE;
    reset_pending = stored_mode == AIRBEE_MODE_RESET_PENDING;
  }
}

static bool store_value(nvm3_ObjectKey_t key, uint8_t value)
{
  for (uint8_t attempt = 0U; attempt < AIRBEE_MODE_WRITE_ATTEMPTS; attempt++) {
    uint8_t verification = UINT8_MAX;
    if (nvm3_writeData(nvm3_defaultHandle, key, &value, sizeof(value))
          == SL_STATUS_OK
        && nvm3_readData(
             nvm3_defaultHandle, key, &verification, sizeof(verification))
             == SL_STATUS_OK
        && verification == value) {
      return true;
    }
  }
  return false;
}

static bool store_mode(airbee_mode_t mode)
{
  return store_value(AIRBEE_MODE_NVM_KEY, (uint8_t)mode);
}

static bool button_is_pressed(void)
{
  return GPIO_PinInGet(AIRBEE_BUTTON_PORT, AIRBEE_BUTTON_PIN) == 0;
}

static bool startup_button_is_pressed(void)
{
  for (uint8_t sample = 0U; sample < AIRBEE_STARTUP_BUTTON_SAMPLES; sample++) {
    sl_sleeptimer_delay_millisecond(AIRBEE_STARTUP_BUTTON_SAMPLE_MS);
    if (!button_is_pressed()) {
      return false;
    }
  }
  return true;
}

static void button_gpio_isr(uint8_t interrupt_number, void *context)
{
  (void)interrupt_number;
  (void)context;
  GPIO_IntDisable(button_wake_mask);
  if (button_thread_id != NULL) {
    (void)osThreadFlagsSet(button_thread_id, 1U);
  }
}

static void rearm_button_wakeup(void)
{
  GPIO_EM4EnablePinWakeup(button_wake_mask, 0U);
  GPIO_IntClear(button_wake_mask);
  GPIO_IntEnable(button_wake_mask);
}

static void button_task(void *argument)
{
  (void)argument;
  const uint32_t debounce_ticks
    = milliseconds_to_ticks(AIRBEE_BUTTON_DEBOUNCE_MS);
  const uint32_t hold_ticks = milliseconds_to_ticks(AIRBEE_MODE_HOLD_MS);
  const uint32_t dfu_ticks = milliseconds_to_ticks(AIRBEE_DFU_HOLD_MS);
  const uint32_t reset_ticks = milliseconds_to_ticks(AIRBEE_RESET_HOLD_MS);
  const uint32_t reset_blink_ticks
    = milliseconds_to_ticks(AIRBEE_RESET_BLINK_MS);
  const uint32_t poll_ticks = milliseconds_to_ticks(AIRBEE_BUTTON_POLL_MS);

  if (calibration_active) {
    bool show_red = true;
    while (calibration_result == 0) {
      if (show_red) {
        zigbee_led_on();
      } else {
        bluetooth_led_on();
      }
      show_red = !show_red;
      (void)osDelay(milliseconds_to_ticks(400UL));
      leds_off();
      (void)osDelay(milliseconds_to_ticks(600UL));
    }
    if (calibration_result > 0) {
      dfu_led_on();
    } else {
      zigbee_led_on();
    }
    for (;;) {
      (void)osThreadFlagsWait(2U, osFlagsWaitAny, osWaitForever);
    }
  }

  blink_mode(active_mode);
  while (button_is_pressed()) {
    (void)osDelay(poll_ticks);
  }
  (void)osDelay(debounce_ticks);
  (void)osThreadFlagsClear(1U);
  rearm_button_wakeup();

  for (;;) {
    (void)osThreadFlagsWait(1U, osFlagsWaitAny, osWaitForever);
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    (void)osDelay(debounce_ticks);
    if (!button_is_pressed()) {
      sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
      rearm_button_wakeup();
      continue;
    }

    const uint32_t press_started = osKernelGetTickCount();
    while (button_is_pressed()) {
      if ((osKernelGetTickCount() - press_started) >= hold_ticks) {
        break;
      }
      (void)osDelay(poll_ticks);
    }
    if (!button_is_pressed()) {
      sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
      rearm_button_wakeup();
      continue;
    }

    const airbee_mode_t next_mode
      = airbee_mode_is_bluetooth() ? AIRBEE_MODE_ZIGBEE : AIRBEE_MODE_BLUETOOTH;
    blink_mode(next_mode);
    bool dfu_selected = false;
    bool reset_selected = false;
    for (;;) {
      if (!button_is_pressed()) {
        (void)osDelay(debounce_ticks);
        if (!button_is_pressed()) {
          break;
        }
      }
      const uint32_t elapsed = osKernelGetTickCount() - press_started;
      if (elapsed >= reset_ticks) {
        reset_selected = true;
        if (((elapsed - reset_ticks) / reset_blink_ticks) % 2U == 0U) {
          zigbee_led_on();
        } else {
          leds_off();
        }
      } else if (elapsed >= dfu_ticks && !dfu_selected) {
        dfu_selected = true;
        blink_dfu();
      }
      (void)osDelay(poll_ticks);
    }

    leds_off();
    if (reset_selected) {
      if (store_mode(AIRBEE_MODE_RESET_PENDING)) {
        NVIC_SystemReset();
      }
    } else if (dfu_selected) {
      sl_apploader_util_reset_to_ota_dfu();
    } else if (store_mode(next_mode)) {
      NVIC_SystemReset();
    }

    leds_off();
    GPIO_PinOutClear(AIRBEE_LED_RED_PORT, AIRBEE_LED_RED_PIN);
    (void)osDelay(milliseconds_to_ticks(1000UL));
    leds_off();
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
    rearm_button_wakeup();
  }
}

void airbee_mode_init(void)
{
  static const osThreadAttr_t button_thread_attributes = {
    .name = "AirBee button",
    .priority = osPriorityLow,
    .stack_size = 4096U,
  };

  GPIO_PinModeSet(
    AIRBEE_LED_RED_PORT, AIRBEE_LED_RED_PIN, gpioModePushPull, AIRBEE_LED_OFF);
  GPIO_PinModeSet(AIRBEE_LED_GREEN_PORT,
                  AIRBEE_LED_GREEN_PIN,
                  gpioModePushPull,
                  AIRBEE_LED_OFF);
  GPIO_PinModeSet(AIRBEE_LED_BLUE_PORT,
                  AIRBEE_LED_BLUE_PIN,
                  gpioModePushPull,
                  AIRBEE_LED_OFF);
  GPIO_PinModeSet(
    AIRBEE_BUTTON_PORT, AIRBEE_BUTTON_PIN, gpioModeInputPullFilter, 1U);

  load_mode();
  if (reset_pending) {
    zigbee_led_on();
    if (airbee_zigbee_reset_network() && store_mode(AIRBEE_MODE_ZIGBEE)) {
      NVIC_SystemReset();
    }
    return;
  }
  calibration_active = startup_button_is_pressed();

  const unsigned int interrupt_number = GPIOINT_EM4WUCallbackRegisterExt(
    AIRBEE_BUTTON_PORT, AIRBEE_BUTTON_PIN, button_gpio_isr, NULL);
  if (interrupt_number != INTERRUPT_UNAVAILABLE) {
    button_wake_mask = 1UL << (interrupt_number + SL_HAL_GPIO_EM4WUEN_SHIFT);
    button_thread_id
      = osThreadNew(button_task, NULL, &button_thread_attributes);
  }
}

bool airbee_mode_is_bluetooth(void)
{
  return active_mode == AIRBEE_MODE_BLUETOOTH;
}

bool airbee_mode_reset_pending(void)
{
  return reset_pending;
}

bool airbee_mode_calibration_active(void)
{
  return calibration_active;
}

void airbee_mode_calibration_complete(bool success)
{
  if (!calibration_active || calibration_result != 0) {
    return;
  }
  calibration_result = success ? 1 : -1;
}
