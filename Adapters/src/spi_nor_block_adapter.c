/**
 * @file spi_nor_block_adapter.c
 * @brief Generic block-device operations backed by an SPI NOR device.
 */
#include "adapters/spi_nor_block_adapter.h"

#include <stddef.h>

#include "spi_nor.h"

static firmware_status_t GetInfo(void *context, block_device_info_t *info)
{
    spi_nor_info_t device_info;
    firmware_status_t status;

    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = SpiNor_GetInfo((spi_nor_t *)context, &device_info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    info->capacity_bytes = device_info.capacity_bytes;
    info->write_size = 1U;
    info->erase_size = device_info.erase_size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    return SpiNor_Read((spi_nor_t *)context, address, data, size);
}

static firmware_status_t Program(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    return SpiNor_Program((spi_nor_t *)context, address, data, size);
}

static firmware_status_t Erase(
    void *context,
    uint32_t address,
    uint32_t size)
{
    return SpiNor_Erase((spi_nor_t *)context, address, size);
}

firmware_status_t SpiNorBlockAdapter_Init(
    spi_nor_block_adapter_t *adapter,
    struct spi_nor *device)
{
    if ((adapter == NULL) || (device == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    adapter->device = device;
    adapter->interface.context = device;
    adapter->interface.get_info = GetInfo;
    adapter->interface.read = Read;
    adapter->interface.program = Program;
    adapter->interface.erase = Erase;
    return FIRMWARE_STATUS_OK;
}

const block_device_t *SpiNorBlockAdapter_Interface(
    const spi_nor_block_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
