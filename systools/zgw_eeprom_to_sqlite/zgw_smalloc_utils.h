#ifndef ZGW_SMALLOC_UTILS_H_
#define ZGW_SMALLOC_UTILS_H_

#include <stdint.h>

#include "smalloc.h"

#ifdef __cplusplus
extern "C"
{
#endif

bool is_smalloc_consistent(const small_memory_device_t *dev, const uint16_t *node_ptrs,
                           uint8_t length, int eeprom_version);

#ifdef __cplusplus
}
#endif

#endif
