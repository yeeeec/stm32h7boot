#ifndef FIRMWARE_BOOT_MANAGER_H
#define FIRMWARE_BOOT_MANAGER_H

#include "firmware/boot_types.h"
#include "firmware/status.h"
#include "ports/storage_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        BOOT_MANAGER_FAST_BOOT = 0,
        BOOT_MANAGER_UPDATED,
        BOOT_MANAGER_RECOVERED,
        BOOT_MANAGER_FATAL
    } boot_manager_result_t;

    /* Application supplies policy operations; the manager only orders them.  This
     * keeps EEPROM/SD/flash details below the Application boundary. */
    typedef struct
    {
        firmware_status_t (*read_boot_control)(void *context, BootControl_t *control);
        firmware_status_t (*clear_boot_control)(void *context);
        firmware_status_t (*storage_init_mount)(void *context);
        firmware_status_t (*storage_unmount)(void *context);
        firmware_status_t (*load_request)(void *context, storage_boot_update_request_t *request);
        firmware_status_t (*perform_update)(void *context,
                                            const storage_boot_update_request_t *request,
                                            int *runtime_modified);
        firmware_status_t (*perform_recovery)(void *context);
        firmware_status_t (*launch_app)(void *context);
        void (*fatal)(void *context, BootError_t error);
        void *context;
    } boot_manager_io_t;

    boot_manager_result_t BootManager_Run(const boot_manager_io_t *io);

#ifdef __cplusplus
}
#endif

#endif
