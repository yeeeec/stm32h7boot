/**
 * @file sdram.c
 * @brief JEDEC-compatible SDR SDRAM power-up sequence implementation.
 */
#include "sdram.h"

#include <stddef.h>

firmware_status_t Sdram_Init(sdram_t *device, const sdram_port_t *port,
                             const sdram_config_t *config)
{
    firmware_status_t status;

    if ((device == NULL) || (port == NULL) || (config == NULL) || (port->send_command == NULL) ||
        (port->set_refresh_rate == NULL) || (port->delay_ms == NULL) ||
        (config->startup_delay_ms == 0U) || (config->auto_refresh_count == 0U) ||
        (config->refresh_rate == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    device->port = *port;

    /*
     * SDR SDRAM requires this order after the FMC pins and clock are ready:
     * clock enable, startup delay, precharge-all, auto-refresh, load-mode,
     * then periodic refresh programming.
     */
    status = device->port.send_command(device->port.context, SDRAM_COMMAND_CLOCK_ENABLE, 1U, 0U);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    device->port.delay_ms(device->port.context, config->startup_delay_ms);

    status = device->port.send_command(device->port.context, SDRAM_COMMAND_PRECHARGE_ALL, 1U, 0U);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = device->port.send_command(device->port.context, SDRAM_COMMAND_AUTO_REFRESH,
                                       config->auto_refresh_count, 0U);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = device->port.send_command(device->port.context, SDRAM_COMMAND_LOAD_MODE, 1U,
                                       config->mode_register);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = device->port.set_refresh_rate(device->port.context, config->refresh_rate);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    device->initialized = 1;
    return FIRMWARE_STATUS_OK;
}
