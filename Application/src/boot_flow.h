#ifndef APPLICATION_BOOT_FLOW_H
#define APPLICATION_BOOT_FLOW_H

#include <stdint.h>

#include "firmware/status.h"

firmware_status_t BootFlow_Run(uint32_t *vector_address);

#endif /* APPLICATION_BOOT_FLOW_H */
