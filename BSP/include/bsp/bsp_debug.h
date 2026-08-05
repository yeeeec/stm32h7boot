#ifndef BSP_DEBUG_H
#define BSP_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

firmware_status_t BSP_DebugWrite(const uint8_t *data, size_t size);

#endif
