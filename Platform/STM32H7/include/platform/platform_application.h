#ifndef PLATFORM_APPLICATION_H
#define PLATFORM_APPLICATION_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t PlatformApplication_Validate(uint32_t vector_address);

    /* Does not return when the jump succeeds. */
    void PlatformApplication_Jump(uint32_t vector_address);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_APPLICATION_H */
