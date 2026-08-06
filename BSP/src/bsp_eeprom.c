/**
 * @file bsp_eeprom.c
 * @brief STM32 HAL I2C1 port for the AT24C128 driver.
 */
#include "bsp/bsp_eeprom.h"

#include <stddef.h>

#include "at24.h"
#include "i2c.h"

#define BSP_EEPROM_TRANSFER_TIMEOUT_MS 100U

static at24_t eeprom;

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

static firmware_status_t I2cRead(
    void *context,
    uint8_t device_address_7bit,
    uint16_t memory_address,
    uint8_t *data,
    uint32_t size)
{
    return HalStatus(HAL_I2C_Mem_Read(
        (I2C_HandleTypeDef *)context,
        (uint16_t)((uint16_t)device_address_7bit << 1U),
        memory_address,
        I2C_MEMADD_SIZE_16BIT,
        data,
        (uint16_t)size,
        BSP_EEPROM_TRANSFER_TIMEOUT_MS));
}

static firmware_status_t I2cWrite(
    void *context,
    uint8_t device_address_7bit,
    uint16_t memory_address,
    const uint8_t *data,
    uint32_t size)
{
    return HalStatus(HAL_I2C_Mem_Write(
        (I2C_HandleTypeDef *)context,
        (uint16_t)((uint16_t)device_address_7bit << 1U),
        memory_address,
        I2C_MEMADD_SIZE_16BIT,
        (uint8_t *)data,
        (uint16_t)size,
        BSP_EEPROM_TRANSFER_TIMEOUT_MS));
}

static firmware_status_t I2cProbeReady(
    void *context,
    uint8_t device_address_7bit,
    int *ready)
{
    HAL_StatusTypeDef status;

    if (ready == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = HAL_I2C_IsDeviceReady(
        (I2C_HandleTypeDef *)context,
        (uint16_t)((uint16_t)device_address_7bit << 1U),
        1U,
        1U);
    /* An AT24 intentionally NACKs while its internal write cycle is active. */
    *ready = (status == HAL_OK) ? 1 : 0;
    return FIRMWARE_STATUS_OK;
}

static uint32_t I2cNowMs(void *context)
{
    (void)context;
    return HAL_GetTick();
}

firmware_status_t BSP_EepromInit(const bsp_eeprom_config_t *config)
{
    at24_port_t port;
    at24_config_t driver_config;
    firmware_status_t status;

    if (config == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((hi2c1.State == HAL_I2C_STATE_RESET) || (eeprom.initialized != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    port.context = &hi2c1;
    port.read = I2cRead;
    port.write = I2cWrite;
    port.probe_ready = I2cProbeReady;
    port.now_ms = I2cNowMs;
    port.set_write_enabled = NULL;
    driver_config.device_address_7bit = config->device_address_7bit;
    driver_config.write_timeout_ms = config->write_timeout_ms;
    status = At24_Init(&eeprom, &port, &driver_config);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return At24_Probe(&eeprom);
}

struct at24 *BSP_EepromDevice(void)
{
    return (eeprom.initialized != 0) ? &eeprom : NULL;
}
