#ifndef PLATFORM_BOOT_CONTROL_H
#define PLATFORM_BOOT_CONTROL_H

#include "firmware/boot_types.h"
#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef firmware_status_t (*platform_boot_control_read_fn)(void *context,
                                                               BootControl_t *control);
    typedef firmware_status_t (*platform_boot_control_write_fn)(void *context,
                                                                const BootControl_t *control);

    typedef struct
    {
        platform_boot_control_read_fn read;
        platform_boot_control_write_fn write;
        void *context;
    } platform_boot_control_port_t;

    void PlatformBootControl_Bind(const platform_boot_control_port_t *port);
    /* Bind the board AT24 device at the reserved BootControl offset. */
    firmware_status_t PlatformBootControl_Init(void);
    firmware_status_t PlatformBootControl_Read(BootControl_t *control);
    firmware_status_t PlatformBootControl_Write(const BootControl_t *control);
    firmware_status_t PlatformBootControl_Clear(void);
    int PlatformBootControl_IsValid(const BootControl_t *control);

#ifdef __cplusplus
}
#endif

#endif
