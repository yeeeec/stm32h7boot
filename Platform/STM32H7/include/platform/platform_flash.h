#ifndef PLATFORM_FLASH_H
#define PLATFORM_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        firmware_status_t (*erase)(void *context, uint32_t address, uint32_t size);
        firmware_status_t (*write)(void *context, uint32_t address, const void *data, size_t size);
        firmware_status_t (*read)(void *context, uint32_t address, void *data, size_t size);
        firmware_status_t (*enter_xip)(void *context);
        firmware_status_t (*exit_xip)(void *context);
        void *context;
    } platform_flash_port_t;

    void PlatformFlash_Bind(const platform_flash_port_t *port);
    firmware_status_t PlatformFlash_Erase(uint32_t address, uint32_t size);
    firmware_status_t PlatformFlash_Write(uint32_t address, const void *data, size_t size);
    firmware_status_t PlatformFlash_Read(uint32_t address, void *data, size_t size);
    firmware_status_t PlatformFlash_EnterXip(void);
    firmware_status_t PlatformFlash_ExitXip(void);

#ifdef __cplusplus
}
#endif

#endif
