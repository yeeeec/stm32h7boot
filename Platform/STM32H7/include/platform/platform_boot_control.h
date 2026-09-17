#ifndef PLATFORM_BOOT_CONTROL_H
#define PLATFORM_BOOT_CONTROL_H

#include <stdint.h>

#include "firmware/status.h"
#include "firmware/boot_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PLATFORM_BOOT_CONTROL_MAGIC          BOOT_MAGIC
#define PLATFORM_BOOT_CONTROL_FORMAT_VERSION 1UL
    typedef BootControl_t platform_boot_control_t;

    firmware_status_t PlatformBootControl_Init(void);

    firmware_status_t PlatformBootControl_Read(BootControl_t *control);

    firmware_status_t PlatformBootControl_Write(const BootControl_t *control);

    firmware_status_t PlatformBootControl_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_BOOT_CONTROL_H */
