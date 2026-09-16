#include "update/app_launcher.h"

#include <string.h>

firmware_status_t AppLauncher_ValidateVector(const app_launcher_config_t *config,
                                             const app_launcher_port_t *port, uint32_t *msp,
                                             uint32_t *reset_handler)
{
    uint32_t vector[2];
    uint32_t app_end;
    if (config == NULL || port == NULL || port->read == NULL || msp == NULL ||
        reset_handler == NULL || config->app_size < 8U || (config->app_address & 3U) != 0U ||
        config->app_address > UINT32_MAX - config->app_size)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    app_end = config->app_address + config->app_size;
    if (FirmwareStatus_IsError(
            port->read(port->context, config->app_address, vector, sizeof(vector))))
        return FIRMWARE_STATUS_IO_ERROR;
    /* Cortex-M vector entries are little-endian 32-bit words.  A valid MSP
     * lies in the configured SRAM window and is naturally aligned. */
    if ((vector[0] & 7U) != 0U || vector[0] < config->sram_start || vector[0] >= config->sram_end)
        return FIRMWARE_STATUS_INVALID_STATE;
    if ((vector[1] & 1U) == 0U || (vector[1] & ~1U) < config->app_address ||
        (vector[1] & ~1U) >= app_end)
        return FIRMWARE_STATUS_INVALID_STATE;
    *msp           = vector[0];
    *reset_handler = vector[1];
    return FIRMWARE_STATUS_OK;
}

firmware_status_t AppLauncher_Launch(const app_launcher_config_t *config,
                                     const app_launcher_port_t *port)
{
    uint32_t msp, reset_handler;
    firmware_status_t status = AppLauncher_ValidateVector(config, port, &msp, &reset_handler);
    if (FirmwareStatus_IsError(status))
        return status;
    if (port->prepare_xip == NULL || port->set_vtor == NULL || port->set_msp == NULL ||
        port->jump == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = port->prepare_xip(port->context);
    if (FirmwareStatus_IsError(status))
        return status;
    if (port->cleanup_interrupts != NULL)
        port->cleanup_interrupts(port->context);
    if (port->invalidate_cache != NULL)
        port->invalidate_cache(port->context);
    port->set_vtor(port->context, config->app_address);
    port->set_msp(port->context, msp);
    port->jump(port->context, reset_handler);
    return FIRMWARE_STATUS_OK;
}
