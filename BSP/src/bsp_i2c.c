#include "bsp/bsp_i2c.h"

#include "i2c.h"
#include "stm32h7xx_hal.h"

#define BSP_I2C_TIMEOUT_MS 100U

#define I2CX hi2c1

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

    if ((HAL_I2C_GetError(&I2CX) & HAL_I2C_ERROR_AF) != 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    return FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t BspI2c_Read(uint8_t address_7bit, uint16_t memory_address, uint8_t *data,
                              uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    return ConvertStatus(HAL_I2C_Mem_Read(&I2CX, (uint16_t) address_7bit << 1U, memory_address,
                                          I2C_MEMADD_SIZE_16BIT, data, (uint16_t) size,
                                          BSP_I2C_TIMEOUT_MS));
}

firmware_status_t BspI2c_Write(uint8_t address_7bit, uint16_t memory_address, const uint8_t *data,
                               uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    return ConvertStatus(HAL_I2C_Mem_Write(&I2CX, (uint16_t) address_7bit << 1U, memory_address,
                                           I2C_MEMADD_SIZE_16BIT, (uint8_t *) data, (uint16_t) size,
                                           BSP_I2C_TIMEOUT_MS));
}

firmware_status_t BspI2c_IsReady(uint8_t address_7bit)
{
    return ConvertStatus(
        HAL_I2C_IsDeviceReady(&I2CX, (uint16_t) address_7bit << 1U, 1U, BSP_I2C_TIMEOUT_MS));
}