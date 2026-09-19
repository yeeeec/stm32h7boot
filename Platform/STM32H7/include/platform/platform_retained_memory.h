#ifndef PLATFORM_RETAINED_MEMORY_H
#define PLATFORM_RETAINED_MEMORY_H

#include <stddef.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PLATFORM_RETAINED_MEMORY_SIZE 64U

    firmware_status_t PlatformRetainedMemory_Read(void *data, size_t size);
    firmware_status_t PlatformRetainedMemory_Write(const void *data, size_t size);
    void PlatformRetainedMemory_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_RETAINED_MEMORY_H */
