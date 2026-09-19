#include "bsp/bsp_at24_bus.h"

#include <stddef.h>
#include <stdint.h>

#include "i2c.h"
#include "stm32h7xx_hal.h"

#define BSP_AT24_BUS_TIMEOUT_MS 100U
#define AT24_I2C_HANDLE         hi2c1

static firmware_status_t ConvertStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    if ((HAL_I2C_GetError(&AT24_I2C_HANDLE) & HAL_I2C_ERROR_AF) != 0U)
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t Validate(uint8_t address_7bit, const void *data, uint32_t size)
{
    if ((address_7bit > 0x7FU) || (data == NULL) || (size == 0U) || (size > UINT16_MAX))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BspAt24Bus_Read(uint8_t address_7bit, uint16_t memory_address, uint8_t *data,
                                  uint32_t size)
{
    firmware_status_t status = Validate(address_7bit, data, size);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    return ConvertStatus(HAL_I2C_Mem_Read(&AT24_I2C_HANDLE, (uint16_t) address_7bit << 1U,
                                           memory_address, I2C_MEMADD_SIZE_16BIT, data,
                                           (uint16_t) size, BSP_AT24_BUS_TIMEOUT_MS));
}

firmware_status_t BspAt24Bus_Write(uint8_t address_7bit, uint16_t memory_address,
                                   const uint8_t *data, uint32_t size)
{
    firmware_status_t status = Validate(address_7bit, data, size);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    return ConvertStatus(HAL_I2C_Mem_Write(&AT24_I2C_HANDLE, (uint16_t) address_7bit << 1U,
                                            memory_address, I2C_MEMADD_SIZE_16BIT,
                                            (uint8_t *) data, (uint16_t) size,
                                            BSP_AT24_BUS_TIMEOUT_MS));
}

firmware_status_t BspAt24Bus_Probe(uint8_t address_7bit)
{
    if (address_7bit > 0x7FU)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return ConvertStatus(HAL_I2C_IsDeviceReady(&AT24_I2C_HANDLE,
                                               (uint16_t) address_7bit << 1U, 1U,
                                               BSP_AT24_BUS_TIMEOUT_MS));
}
