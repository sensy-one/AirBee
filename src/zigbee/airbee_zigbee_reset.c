#include "airbee_zigbee.h"

#include <stddef.h>

#include "nvm3.h"
#include "nvm3_default.h"
#include "sl_component_catalog.h"
#include "stack/config/sl_zigbee_token_defines.h"

#ifndef SL_CATALOG_ZIGBEE_CLASSIC_KEY_STORAGE_PRESENT
#error "AirBee reset must be updated before enabling Zigbee secure key storage"
#endif

static bool preserve_token(nvm3_ObjectKey_t key)
{
  static const uint32_t preserved[] = {
    COMMON_TOKEN_STACK_NONCE_COUNTER,
    COMMON_TOKEN_STACK_APS_FRAME_COUNTER,
    COMMON_TOKEN_MULTI_NETWORK_STACK_NONCE_COUNTER,
    COMMON_TOKEN_STACK_BOOT_COUNTER,
    COMMON_TOKEN_STACK_RESTORED_EUI64,
  };
  for (size_t i = 0U; i < sizeof(preserved) / sizeof(preserved[0]); ++i) {
    if (key == (preserved[i] & NVM3_KEY_MASK)) {
      return true;
    }
  }
  return false;
}

bool airbee_zigbee_reset_network(void)
{
  nvm3_ObjectKey_t keys[16];
  for (;;) {
    const size_t count = nvm3_enumObjects(nvm3_defaultHandle,
                                         keys,
                                         sizeof(keys) / sizeof(keys[0]),
                                         SL_TOKEN_NVM3_REGION_ZIGBEE,
                                         SL_TOKEN_NVM3_REGION_THREAD - 1U);
    bool deleted = false;
    for (size_t i = 0U; i < count; ++i) {
      if (!preserve_token(keys[i])) {
        if (nvm3_deleteObject(nvm3_defaultHandle, keys[i]) != SL_STATUS_OK) {
          return false;
        }
        deleted = true;
      }
    }
    if (!deleted) {
      return true;
    }
  }
}
