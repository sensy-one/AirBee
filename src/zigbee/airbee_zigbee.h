#pragma once

#include <stdbool.h>

typedef void (*airbee_zigbee_connection_callback_t)(bool connected);

void airbee_zigbee_init(bool enabled,
                        airbee_zigbee_connection_callback_t callback);
bool airbee_zigbee_connected(void);
void airbee_zigbee_publish(void);
