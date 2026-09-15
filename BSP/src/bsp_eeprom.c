/**
 * @file bsp_eeprom.c
 * @brief AT24C128 驱动的板级 STM32 I2C 绑定。
 *
 * 可移植驱动负责页边界和写周期 Policy。本模块持有静态驱动实例，将未移位的
 * 7-bit 设备地址转换为 HAL 约定，并将传输结果映射为 Firmware Status。
 * EEPROM 就绪状态通过 acknowledge poll 生命周期确认：NACK 本身不能证明传输
 * 失败，因为设备在提交 Page 时会有意返回 NACK。
 */
#include "bsp/bsp_eeprom.h"

#include <stddef.h>

#include "at24.h"
#include "i2c.h"
#include "stm32h7xx_hal.h"

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

static firmware_status_t I2cRead(void *context, uint8_t device_address_7bit,
                                 uint16_t memory_address, uint8_t *data, uint32_t size)
{
    return HalStatus(HAL_I2C_Mem_Read((I2C_HandleTypeDef *) context,
                                      (uint16_t) ((uint16_t) device_address_7bit << 1U),
                                      memory_address, I2C_MEMADD_SIZE_16BIT, data, (uint16_t) size,
                                      BSP_EEPROM_TRANSFER_TIMEOUT_MS));
}

static firmware_status_t I2cWrite(void *context, uint8_t device_address_7bit,
                                  uint16_t memory_address, const uint8_t *data, uint32_t size)
{
    return HalStatus(HAL_I2C_Mem_Write((I2C_HandleTypeDef *) context,
                                       (uint16_t) ((uint16_t) device_address_7bit << 1U),
                                       memory_address, I2C_MEMADD_SIZE_16BIT, (uint8_t *) data,
                                       (uint16_t) size, BSP_EEPROM_TRANSFER_TIMEOUT_MS));
}

static firmware_status_t I2cProbeReady(void *context, uint8_t device_address_7bit, int *ready)
{
    HAL_StatusTypeDef status;

    if (ready == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = HAL_I2C_IsDeviceReady((I2C_HandleTypeDef *) context,
                                   (uint16_t) ((uint16_t) device_address_7bit << 1U), 1U, 1U);
    /*
     * HAL 无法区分预期的 Busy NACK 与临时探测失败。两者都报告为未就绪，
     * 由 AT24 驱动的有界写周期 Timeout 决定最终结果。
     */
    *ready = (status == HAL_OK) ? 1 : 0;
    return FIRMWARE_STATUS_OK;
}

static uint32_t I2cNowMs(void *context)
{
    (void) context;
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

    port.context                      = &hi2c1;
    port.read                         = I2cRead;
    port.write                        = I2cWrite;
    port.probe_ready                  = I2cProbeReady;
    port.now_ms                       = I2cNowMs;
    port.set_write_enabled            = NULL;
    driver_config.device_address_7bit = config->device_address_7bit;
    driver_config.write_timeout_ms    = config->write_timeout_ms;
    status                            = At24_Init(&eeprom, &port, &driver_config);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /*
     * 探测失败是本次 Boot 的终态。驱动已经绑定并被保留，因此重试初始化会
     * 违反其生命周期约束。
     */
    return At24_Probe(&eeprom);
}

struct at24 *BSP_EepromDevice(void)
{
    return (eeprom.initialized != 0) ? &eeprom : NULL;
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
        if (!FirmwareStatus_IsOk(status))
            return status;
        (void) At24_GetOperationResult(&eeprom, &result);
        if ((result.state == AT24_OPERATION_SUCCEEDED) ||
            (result.state == AT24_OPERATION_FAILED))
            return result.status;
        HAL_Delay(1U);
    }
}

firmware_status_t BSP_EepromWrite(uint32_t address, const void *data, uint32_t size)
{
    const uint8_t *source = (const uint8_t *) data;
    uint32_t remaining = size;

    if (eeprom.initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    /* Validate the complete request before starting page writes.  Without
     * this guard an out-of-range request could partially modify the EEPROM
     * and only fail when the final page is submitted. */
    if ((data == NULL) || (size == 0U) || (address >= BSP_EEPROM_CAPACITY_BYTES) ||
        (size > (BSP_EEPROM_CAPACITY_BYTES - address)))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    while (remaining != 0U)
    {
        const uint32_t page_offset = address % AT24C128_PAGE_SIZE_BYTES;
        const uint32_t page_space = AT24C128_PAGE_SIZE_BYTES - page_offset;
        const uint32_t chunk = remaining < page_space ? remaining : page_space;
        firmware_status_t status = At24_WritePageStart(&eeprom, address, source, chunk);

        if (!FirmwareStatus_IsOk(status))
            return status;
        status = WaitForPageWrite();
        if (!FirmwareStatus_IsOk(status))
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
