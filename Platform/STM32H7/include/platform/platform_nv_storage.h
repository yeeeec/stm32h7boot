#ifndef PLATFORM_NV_STORAGE_H
#define PLATFORM_NV_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t PlatformNvStorage_Init(void);
    firmware_status_t PlatformNvStorage_Read(uint32_t offset, void *data, size_t size);
    firmware_status_t PlatformNvStorage_Write(uint32_t offset, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_NV_STORAGE_H */
