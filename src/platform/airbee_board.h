#pragma once

#include "em_device.h"
#include "em_gpio.h"
#include "sl_gpio.h"

#define AIRBEE_I2C_PERIPHERAL I2C0
#define AIRBEE_I2C_SDA_PORT   SL_GPIO_PORT_B
#define AIRBEE_I2C_SDA_PIN    4U
#define AIRBEE_I2C_SCL_PORT   SL_GPIO_PORT_B
#define AIRBEE_I2C_SCL_PIN    3U

#define AIRBEE_SUN_SWITCH_PORT gpioPortD
#define AIRBEE_SUN_SWITCH_PIN  0U
#define AIRBEE_SUN_ENABLE_PORT gpioPortC
#define AIRBEE_SUN_ENABLE_PIN  2U
#define AIRBEE_SUN_NRDY_PORT   gpioPortC
#define AIRBEE_SUN_NRDY_PIN    1U

#define AIRBEE_LED_RED_PORT         gpioPortA
#define AIRBEE_LED_RED_PIN          7U
#define AIRBEE_LED_GREEN_PORT       gpioPortA
#define AIRBEE_LED_GREEN_PIN        8U
#define AIRBEE_LED_BLUE_PORT        gpioPortD
#define AIRBEE_LED_BLUE_PIN         3U
#define AIRBEE_BUTTON_PORT          gpioPortD
#define AIRBEE_BUTTON_PIN           2U
#define AIRBEE_SUN_BUS_CONNECTED    1U
#define AIRBEE_SUN_BUS_DISCONNECTED 0U
#define AIRBEE_SUN_ENABLED          1U
#define AIRBEE_SUN_DISABLED         0U
#define AIRBEE_LED_OFF              1U
