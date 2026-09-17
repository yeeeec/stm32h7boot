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

static at24_status_t SetWriteEnabled(at24_t *device, int enabled)
{
    if (device->port.set_write_enabled == NULL)
    {
        return AT24_STATUS_OK;
    }
    return device->port.set_write_enabled(device->port.context, enabled);
}

static at24_status_t FinishFailure(at24_t *device, at24_status_t status)
{
    at24_status_t protect_status = SetWriteEnabled(device, 0);

    device->operation_state  = AT24_OPERATION_FAILED;
    device->operation_status = (protect_status == AT24_STATUS_OK) ? status : protect_status;
    return device->operation_status;
}

at24_status_t At24_Init(at24_t *device, const at24_port_t *port, const at24_config_t *config)
{
    if ((device == NULL) || (port == NULL) || (config == NULL) || (port->read == NULL) ||
        (port->write == NULL) || (port->probe_ready == NULL) || (port->now_ms == NULL) ||
        (config->device_address_7bit < AT24C128_ADDRESS_MIN_7BIT) ||
        (config->device_address_7bit > AT24C128_ADDRESS_MAX_7BIT) ||
        (config->write_timeout_ms == 0U))
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return AT24_STATUS_INVALID_STATE;
    }

    device->port                = *port;
    device->write_timeout_ms    = config->write_timeout_ms;
    device->write_started_ms    = 0U;
    device->operation_status    = AT24_STATUS_OK;
    device->operation_state     = AT24_OPERATION_IDLE;
    device->device_address_7bit = config->device_address_7bit;
    device->initialized         = 1;
    return AT24_STATUS_OK;
}

at24_status_t At24_Probe(at24_t *device)
{
    int ready;

    if (device == NULL)
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return AT24_STATUS_INVALID_STATE;
    }
    ready = device->port.probe_ready(device->port.context, device->device_address_7bit);
    return (ready == 0) ? AT24_STATUS_OK : AT24_STATUS_IO_ERROR;
}

at24_status_t At24_Read(at24_t *device, uint32_t address, void *data, uint32_t size)
{
    if ((device == NULL) || (data == NULL) || !RangeIsValid(address, size))
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if ((device->initialized == 0) || (device->operation_state == AT24_OPERATION_BUSY))
    {
        return AT24_STATUS_INVALID_STATE;
    }
    return device->port.read(device->port.context, device->device_address_7bit, (uint16_t) address,
                             (uint8_t *) data, size);
}

at24_status_t At24_WritePageStart(at24_t *device, uint32_t address, const void *data, uint32_t size)
{
    at24_status_t status;

    if ((device == NULL) || (data == NULL) || !RangeIsValid(address, size) ||
        (size > AT24C128_PAGE_SIZE_BYTES) ||
        ((address % AT24C128_PAGE_SIZE_BYTES) > (AT24C128_PAGE_SIZE_BYTES - size)))
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if ((device->initialized == 0) || (device->operation_state == AT24_OPERATION_BUSY))
    {
        return AT24_STATUS_INVALID_STATE;
    }

    status = SetWriteEnabled(device, 1);
    if (status != AT24_STATUS_OK)
    {
        device->operation_state  = AT24_OPERATION_FAILED;
        device->operation_status = status;
        return status;
    }
    status = device->port.write(device->port.context, device->device_address_7bit,
                                (uint16_t) address, (const uint8_t *) data, size);
    if (status != AT24_STATUS_OK)
    {
        return FinishFailure(device, status);
    }

    device->write_started_ms = device->port.now_ms(device->port.context);
    device->operation_status = AT24_STATUS_OK;
    device->operation_state  = AT24_OPERATION_BUSY;
    return AT24_STATUS_OK;
}

at24_status_t At24_OperationPoll(at24_t *device)
{
    at24_status_t status;
    int ready;

    if (device == NULL)
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return AT24_STATUS_INVALID_STATE;
    }
    if (device->operation_state != AT24_OPERATION_BUSY)
    {
        return AT24_STATUS_OK;
    }

    ready = device->port.probe_ready(device->port.context, device->device_address_7bit);
    if (ready == 0)
    {
        status = SetWriteEnabled(device, 0);
        if (status != AT24_STATUS_OK)
        {
            device->operation_state  = AT24_OPERATION_FAILED;
            device->operation_status = status;
            return status;
        }
        device->operation_state  = AT24_OPERATION_SUCCEEDED;
        device->operation_status = AT24_STATUS_OK;
        return AT24_STATUS_OK;
    }

    /* Unsigned subtraction remains valid when the millisecond tick wraps. */
    if ((uint32_t) (device->port.now_ms(device->port.context) - device->write_started_ms) >=
        device->write_timeout_ms)
    {
        return FinishFailure(device, AT24_STATUS_TIMEOUT);
    }
    return AT24_STATUS_OK;
}

at24_status_t At24_GetOperationResult(const at24_t *device, at24_operation_result_t *result)
{
    if ((device == NULL) || (result == NULL))
    {
        return AT24_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return AT24_STATUS_INVALID_STATE;
    }
    result->state  = device->operation_state;
    result->status = device->operation_status;
    return AT24_STATUS_OK;
}
