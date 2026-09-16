#ifndef FIRMWARE_APP_LAUNCHER_H
#define FIRMWARE_APP_LAUNCHER_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint32_t app_address;
        uint32_t app_size;
        uint32_t sram_start;
        uint32_t sram_end;
    } app_launcher_config_t;

    typedef firmware_status_t (*app_read_fn)(void *context, uint32_t address, void *data,
                                             uint32_t size);
    typedef firmware_status_t (*app_prepare_xip_fn)(void *context);
    typedef void (*app_cache_barrier_fn)(void *context);
    typedef void (*app_set_vtor_fn)(void *context, uint32_t address);
    typedef void (*app_set_msp_fn)(void *context, uint32_t value);
    typedef void (*app_jump_fn)(void *context, uint32_t reset_handler);

    typedef struct
    {
        app_read_fn read;
        app_prepare_xip_fn prepare_xip;
        app_cache_barrier_fn invalidate_cache;
        app_set_vtor_fn set_vtor;
        app_set_msp_fn set_msp;
        app_jump_fn jump;
        app_cache_barrier_fn cleanup_interrupts;
        void *context;
    } app_launcher_port_t;

    firmware_status_t AppLauncher_ValidateVector(const app_launcher_config_t *config,
                                                 const app_launcher_port_t *port, uint32_t *msp,
                                                 uint32_t *reset_handler);
    firmware_status_t AppLauncher_Launch(const app_launcher_config_t *config,
                                         const app_launcher_port_t *port);

#ifdef __cplusplus
}
#endif

#endif
