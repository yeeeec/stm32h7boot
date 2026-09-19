#ifndef PLATFORM_RUNTIME_H
#define PLATFORM_RUNTIME_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t PlatformRuntime_Validate(uint32_t vector_address, uint32_t region_size);
    _Noreturn void PlatformRuntime_Start(uint32_t vector_address);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_RUNTIME_H */
