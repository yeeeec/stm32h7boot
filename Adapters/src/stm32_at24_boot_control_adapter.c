/**
 * @file stm32_at24_boot_control_adapter.c
 * @brief HAL I2C implementation of the AT24 Boot Control store.
 */
#include "adapters/stm32_at24_boot_control_adapter.h"

#include <limits.h>
#include <stddef.h>

#include "stm32h7xx_hal.h"

#define AT24_MEMORY_ADDRESS_SIZE I2C_MEMADD_SIZE_16BIT
#define AT24_TRANSFER_TIMEOUT_MS 100U

static firmware_status_t HalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t SetWriteEnabled(
    stm32_at24_boot_control_adapter_t *adapter,
    int enabled)
{
    if (adapter->set_write_enabled == NULL)
    {
        return FIRMWARE_STATUS_OK;
    }
    return adapter->set_write_enabled(
        adapter->write_protect_context, enabled);
}

static firmware_status_t GetInfo(
    void *context,
    boot_control_store_info_t *info)
{
    const stm32_at24_boot_control_adapter_t *adapter =
        (const stm32_at24_boot_control_adapter_t *)context;

    if ((adapter == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    info->capacity_bytes = adapter->capacity_bytes;
    info->page_size = adapter->page_size;
    return FIRMWARE_STATUS_OK;
}

static int RangeIsValid(
    const stm32_at24_boot_control_adapter_t *adapter,
    uint32_t address,
    uint32_t size)
{
    return (size != 0U) && (address < adapter->capacity_bytes) &&
           (size <= (adapter->capacity_bytes - address)) &&
           (size <= UINT16_MAX);
}

static firmware_status_t Read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    stm32_at24_boot_control_adapter_t *adapter =
        (stm32_at24_boot_control_adapter_t *)context;

    if ((adapter == NULL) || (data == NULL) ||
        !RangeIsValid(adapter, address, size))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->write_pending != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    return HalStatus(HAL_I2C_Mem_Read(
        adapter->i2c_handle,
        adapter->hal_device_address,
        (uint16_t)address,
        AT24_MEMORY_ADDRESS_SIZE,
        (uint8_t *)data,
        (uint16_t)size,
        AT24_TRANSFER_TIMEOUT_MS));
}

static firmware_status_t WritePage(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    stm32_at24_boot_control_adapter_t *adapter =
        (stm32_at24_boot_control_adapter_t *)context;
    firmware_status_t status;

    if ((adapter == NULL) || (data == NULL) ||
        !RangeIsValid(adapter, address, size) ||
        (size > adapter->page_size) ||
        ((address % adapter->page_size) > (adapter->page_size - size)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->write_pending != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = SetWriteEnabled(adapter, 1);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = HalStatus(HAL_I2C_Mem_Write(
        adapter->i2c_handle,
        adapter->hal_device_address,
        (uint16_t)address,
        AT24_MEMORY_ADDRESS_SIZE,
        (uint8_t *)data,
        (uint16_t)size,
        AT24_TRANSFER_TIMEOUT_MS));
    if (!FirmwareStatus_IsOk(status))
    {
        firmware_status_t protect_status = SetWriteEnabled(adapter, 0);

        return FirmwareStatus_IsOk(protect_status) ? status : protect_status;
    }

    adapter->write_started_ms = HAL_GetTick();
    adapter->write_pending = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t IsReady(void *context, int *ready)
{
    stm32_at24_boot_control_adapter_t *adapter =
        (stm32_at24_boot_control_adapter_t *)context;
    HAL_StatusTypeDef hal_status;
    firmware_status_t status;

    if ((adapter == NULL) || (ready == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->write_pending == 0)
    {
        *ready = 1;
        return FIRMWARE_STATUS_OK;
    }

    hal_status = HAL_I2C_IsDeviceReady(
        adapter->i2c_handle,
        adapter->hal_device_address,
        1U,
        1U);
    if (hal_status == HAL_OK)
    {
        adapter->write_pending = 0;
        status = SetWriteEnabled(adapter, 0);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        *ready = 1;
        return FIRMWARE_STATUS_OK;
    }

    /* AT24 devices NACK during the internal write cycle; bound that state by time. */
    if ((uint32_t)(HAL_GetTick() - adapter->write_started_ms) >=
        adapter->write_timeout_ms)
    {
        adapter->write_pending = 0;
        status = SetWriteEnabled(adapter, 0);
        return FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_TIMEOUT : status;
    }

    *ready = 0;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32At24BootControlAdapter_Init(
    stm32_at24_boot_control_adapter_t *adapter,
    const stm32_at24_boot_control_config_t *config)
{
    if ((adapter == NULL) || (config == NULL) ||
        (config->i2c_handle == NULL) ||
        (config->device_address_7bit > 0x7FU) ||
        (config->capacity_bytes == 0U) || (config->page_size == 0U) ||
        (config->page_size > config->capacity_bytes) ||
        (config->write_timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (config->capacity_bytes > 0x10000UL)
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    adapter->i2c_handle = config->i2c_handle;
    adapter->write_protect_context = config->write_protect_context;
    adapter->set_write_enabled = config->set_write_enabled;
    adapter->capacity_bytes = config->capacity_bytes;
    adapter->page_size = config->page_size;
    adapter->write_timeout_ms = config->write_timeout_ms;
    adapter->write_started_ms = 0U;
    adapter->hal_device_address =
        (uint16_t)((uint16_t)config->device_address_7bit << 1U);
    adapter->write_pending = 0;
    adapter->interface.context = adapter;
    adapter->interface.get_info = GetInfo;
    adapter->interface.read = Read;
    adapter->interface.write_page = WritePage;
    adapter->interface.is_ready = IsReady;
    return FIRMWARE_STATUS_OK;
}

const boot_control_store_t *Stm32At24BootControlAdapter_Interface(
    const stm32_at24_boot_control_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
