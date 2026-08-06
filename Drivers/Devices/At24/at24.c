/**
 * @file at24.c
 * @brief Hardware-independent AT24C128 EEPROM implementation.
 */
#include "at24.h"

#include <stddef.h>

static int RangeIsValid(uint32_t address, uint32_t size)
{
    return (size != 0U) && (address < AT24C128_CAPACITY_BYTES) &&
           (size <= (AT24C128_CAPACITY_BYTES - address));
}

static firmware_status_t SetWriteEnabled(at24_t *device, int enabled)
{
    if (device->port.set_write_enabled == NULL)
    {
        return FIRMWARE_STATUS_OK;
    }
    return device->port.set_write_enabled(device->port.context, enabled);
}

static firmware_status_t FinishFailure(
    at24_t *device,
    firmware_status_t status)
{
    firmware_status_t protect_status = SetWriteEnabled(device, 0);

    device->operation_state = AT24_OPERATION_FAILED;
    device->operation_status = FirmwareStatus_IsOk(protect_status)
                                   ? status
                                   : protect_status;
    return device->operation_status;
}

firmware_status_t At24_Init(
    at24_t *device,
    const at24_port_t *port,
    const at24_config_t *config)
{
    if ((device == NULL) || (port == NULL) || (config == NULL) ||
        (port->read == NULL) || (port->write == NULL) ||
        (port->probe_ready == NULL) || (port->now_ms == NULL) ||
        (config->device_address_7bit < AT24C128_ADDRESS_MIN_7BIT) ||
        (config->device_address_7bit > AT24C128_ADDRESS_MAX_7BIT) ||
        (config->write_timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    device->port = *port;
    device->write_timeout_ms = config->write_timeout_ms;
    device->write_started_ms = 0U;
    device->operation_status = FIRMWARE_STATUS_OK;
    device->operation_state = AT24_OPERATION_IDLE;
    device->device_address_7bit = config->device_address_7bit;
    device->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t At24_GetInfo(const at24_t *device, at24_info_t *info)
{
    if ((device == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    info->capacity_bytes = AT24C128_CAPACITY_BYTES;
    info->page_size = AT24C128_PAGE_SIZE_BYTES;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t At24_Probe(at24_t *device)
{
    int ready;
    firmware_status_t status;

    if (device == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = device->port.probe_ready(
        device->port.context, device->device_address_7bit, &ready);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return (ready != 0) ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t At24_Read(
    at24_t *device,
    uint32_t address,
    void *data,
    uint32_t size)
{
    if ((device == NULL) || (data == NULL) ||
        !RangeIsValid(address, size))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (device->operation_state == AT24_OPERATION_BUSY)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return device->port.read(
        device->port.context,
        device->device_address_7bit,
        (uint16_t)address,
        (uint8_t *)data,
        size);
}

firmware_status_t At24_WritePageStart(
    at24_t *device,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    firmware_status_t status;

    if ((device == NULL) || (data == NULL) ||
        !RangeIsValid(address, size) ||
        (size > AT24C128_PAGE_SIZE_BYTES) ||
        ((address % AT24C128_PAGE_SIZE_BYTES) >
         (AT24C128_PAGE_SIZE_BYTES - size)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((device->initialized == 0) ||
        (device->operation_state == AT24_OPERATION_BUSY))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = SetWriteEnabled(device, 1);
    if (!FirmwareStatus_IsOk(status))
    {
        device->operation_state = AT24_OPERATION_FAILED;
        device->operation_status = status;
        return status;
    }
    status = device->port.write(
        device->port.context,
        device->device_address_7bit,
        (uint16_t)address,
        (const uint8_t *)data,
        size);
    if (!FirmwareStatus_IsOk(status))
    {
        return FinishFailure(device, status);
    }

    device->write_started_ms = device->port.now_ms(device->port.context);
    device->operation_status = FIRMWARE_STATUS_OK;
    device->operation_state = AT24_OPERATION_BUSY;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t At24_OperationPoll(at24_t *device)
{
    firmware_status_t status;
    int ready;

    if (device == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (device->operation_state != AT24_OPERATION_BUSY)
    {
        return FIRMWARE_STATUS_OK;
    }

    status = device->port.probe_ready(
        device->port.context, device->device_address_7bit, &ready);
    if (!FirmwareStatus_IsOk(status))
    {
        return FinishFailure(device, status);
    }
    if (ready != 0)
    {
        status = SetWriteEnabled(device, 0);
        if (!FirmwareStatus_IsOk(status))
        {
            device->operation_state = AT24_OPERATION_FAILED;
            device->operation_status = status;
            return status;
        }
        device->operation_state = AT24_OPERATION_SUCCEEDED;
        device->operation_status = FIRMWARE_STATUS_OK;
        return FIRMWARE_STATUS_OK;
    }

    /* Unsigned subtraction keeps the timeout valid across tick wraparound. */
    if ((uint32_t)(device->port.now_ms(device->port.context) -
                   device->write_started_ms) >= device->write_timeout_ms)
    {
        return FinishFailure(device, FIRMWARE_STATUS_TIMEOUT);
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t At24_GetOperationResult(
    const at24_t *device,
    at24_operation_result_t *result)
{
    if ((device == NULL) || (result == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    result->state = device->operation_state;
    result->status = device->operation_status;
    return FIRMWARE_STATUS_OK;
}
