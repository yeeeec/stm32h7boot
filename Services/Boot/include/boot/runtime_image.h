#ifndef BOOT_RUNTIME_IMAGE_H
#define BOOT_RUNTIME_IMAGE_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t RuntimeImage_Prepare(uint32_t *vector_address);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_RUNTIME_IMAGE_H */
