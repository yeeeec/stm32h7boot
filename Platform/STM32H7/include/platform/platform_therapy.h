#ifndef PLATFORM_THERAPY_H
#define PLATFORM_THERAPY_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t bootloader_version;
        uint16_t device_id;
    } platform_therapy_info_t;

    firmware_status_t PlatformTherapy_Init(void);
    firmware_status_t PlatformTherapy_BeginUpdate(platform_therapy_info_t *info);

    firmware_status_t PlatformTherapy_Read(uint32_t address, void *data, uint32_t size);

    firmware_status_t PlatformTherapy_Erase(uint32_t address, uint32_t size);

    firmware_status_t PlatformTherapy_Write(uint32_t address, const void *data, uint32_t size);

    firmware_status_t PlatformTherapy_Verify(uint32_t address, const void *data, uint32_t size);

    firmware_status_t PlatformTherapy_EndUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_THERAPY_H */
