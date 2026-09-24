#include <stdint.h>

#include "api/btl_errorcode.h"
#include "em_cmu.h"
#include "em_device.h"
#include "em_gpio.h"
#include "em_letimer.h"
#include "sl_apploader_lib_api.h"

#define AIRBEE_DFU_LED_PORT      gpioPortA
#define AIRBEE_DFU_LED_PIN       8U
#define AIRBEE_DFU_TIMEOUT_TICKS 600U
#define AIRBEE_DFU_RESTART_TICKS 6U

static uint32_t dfu_ticks;
static volatile uint32_t restart_ticks;

int32_t __real_bootloader_apploader_parser_finish(void);

int32_t __wrap_bootloader_apploader_parser_finish(void)
{
  const int32_t result = __real_bootloader_apploader_parser_finish();
  if (result == BOOTLOADER_OK) {
    restart_ticks = AIRBEE_DFU_RESTART_TICKS;
  }
  return result;
}

void LETIMER0_IRQHandler(void)
{
  const uint32_t flags = LETIMER_IntGetEnabled(LETIMER0);
  LETIMER_IntClear(LETIMER0, flags);
  GPIO_PinOutToggle(AIRBEE_DFU_LED_PORT, AIRBEE_DFU_LED_PIN);

  if (restart_ticks > 0U && --restart_ticks == 0U) {
    NVIC_SystemReset();
  }
  if (++dfu_ticks >= AIRBEE_DFU_TIMEOUT_TICKS) {
    NVIC_SystemReset();
  }
}

static void start_status_timer(void)
{
  CMU_ClockSelectSet(cmuClock_EM23GRPACLK, cmuSelect_LFRCO);
  CMU_ClockEnable(cmuClock_LETIMER0, true);

  LETIMER_Init_TypeDef timer = LETIMER_INIT_DEFAULT;
  timer.enable = false;
  timer.comp0Top = true;
  timer.topValue = CMU_ClockFreqGet(cmuClock_LETIMER0) / 2U;
  LETIMER_Init(LETIMER0, &timer);
  LETIMER_IntClear(LETIMER0, _LETIMER_IF_MASK);
  LETIMER_IntEnable(LETIMER0, LETIMER_IEN_UF);
  NVIC_ClearPendingIRQ(LETIMER0_IRQn);
  NVIC_EnableIRQ(LETIMER0_IRQn);
  LETIMER_Enable(LETIMER0, true);
}

void bootloader_apploader_get_custom_device_address(
  sl_apploader_address_t *address)
{
  const uint8_t *token = (const uint8_t *)USERDATA_BASE + 2U;
  const uint8_t *device_address = &token[2];

  for (uint8_t index = 0U; index < sizeof(address->address); index++) {
    address->address[index]
      = device_address[sizeof(address->address) - 1U - index];
  }
  address->type = token[1] == 1U ? sl_apploader_address_type_random
                                 : sl_apploader_address_type_public;

#if defined(_CMU_CLKEN0_MASK)
  CMU->CLKEN0_SET = CMU_CLKEN0_GPIO;
#endif
  GPIO_PinModeSet(
    AIRBEE_DFU_LED_PORT, AIRBEE_DFU_LED_PIN, gpioModePushPull, 1U);
  dfu_ticks = 0U;
  restart_ticks = 0U;
  start_status_timer();
}
