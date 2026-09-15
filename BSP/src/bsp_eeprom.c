/**
 * @file bsp_eeprom.c
 * @brief AT24C128 binding to the board's shared I2C1 bus.
 */
#include "bsp/bsp_eeprom.h"

#include <stddef.h>

#include "at24.h"
#include "bsp/bsp_i2c_bus.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

#define BSP_EEPROM_TRANSFER_TIMEOUT_MS 100U

static at24_t eeprom;

static firmware_status_t I2cRead(void *context, uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                 uint8_t *data, uint32_t size)
{
    (void) context;
    if (size > UINT16_MAX)
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    return BSP_I2c1MemRead(deviceAddress7bit, memoryAddress, BSP_I2C_MEMORY_ADDRESS_16BIT, data,
                           (uint16_t) size, BSP_EEPROM_TRANSFER_TIMEOUT_MS);
}

static firmware_status_t I2cWrite(void *context, uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                  const uint8_t *data, uint32_t size)
{
    (void) context;
    if (size > UINT16_MAX)
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    return BSP_I2c1MemWrite(deviceAddress7bit, memoryAddress, BSP_I2C_MEMORY_ADDRESS_16BIT, data,
                            (uint16_t) size, BSP_EEPROM_TRANSFER_TIMEOUT_MS);
}

static firmware_status_t I2cProbeReady(void *context, uint8_t deviceAddress7bit, int *ready)
{
    (void) context;
    return BSP_I2c1Probe(deviceAddress7bit, 1U, ready);
}

static uint32_t I2cNowMs(void *context)
{
    (void) context;
    return HAL_GetTick();
}

firmware_status_t BSP_EepromInit(const bsp_eeprom_config_t *config)
{
    at24_port_t port;
    at24_config_t driverConfig;
    firmware_status_t status;

    if (config == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;
    if (eeprom.initialized != 0)
        return FIRMWARE_STATUS_OK;

    port.context                     = NULL;
    port.read                        = I2cRead;
    port.write                       = I2cWrite;
    port.probe_ready                 = I2cProbeReady;
    port.now_ms                      = I2cNowMs;
    port.set_write_enabled           = NULL;
    driverConfig.device_address_7bit = config->device_address_7bit;
    driverConfig.write_timeout_ms    = config->write_timeout_ms;
    status                           = At24_Init(&eeprom, &port, &driverConfig);
    if (FirmwareStatus_IsError(status))
        return status;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_EepromProbe(void)
{
    if (eeprom.initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    return At24_Probe(&eeprom);
}

firmware_status_t BSP_EepromRead(uint32_t address, void *data, uint32_t size)
{
    if (eeprom.initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    return At24_Read(&eeprom, address, data, size);
}

static firmware_status_t WaitForPageWrite(void)
{
    at24_operation_result_t result;

    for (;;)
    {
        firmware_status_t status = At24_OperationPoll(&eeprom);
        if (FirmwareStatus_IsError(status))
            return status;
        (void) At24_GetOperationResult(&eeprom, &result);
        if (result.state == AT24_OPERATION_SUCCEEDED || result.state == AT24_OPERATION_FAILED)
            return result.status;
        if (osKernelGetState() == osKernelRunning)
            (void) osDelay(1U);
        else
            HAL_Delay(1U);
    }
}

firmware_status_t BSP_EepromWrite(uint32_t address, const void *data, uint32_t size)
{
    const uint8_t *source = (const uint8_t *) data;
    uint32_t remaining    = size;

    if (eeprom.initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (data == NULL || size == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    while (remaining != 0U)
    {
        const uint32_t page_offset = address % BSP_EEPROM_PAGE_SIZE_BYTES;
        const uint32_t page_space  = BSP_EEPROM_PAGE_SIZE_BYTES - page_offset;
        const uint32_t chunk       = remaining < page_space ? remaining : page_space;
        firmware_status_t status   = At24_WritePageStart(&eeprom, address, source, chunk);
        if (FirmwareStatus_IsError(status))
            return status;
        status = WaitForPageWrite();
        if (FirmwareStatus_IsError(status))
            return status;
        address += chunk;
        source += chunk;
        remaining -= chunk;
    }
    return FIRMWARE_STATUS_OK;
}

int BSP_EepromIsInitialized(void)
{
    return eeprom.initialized != 0;
}
