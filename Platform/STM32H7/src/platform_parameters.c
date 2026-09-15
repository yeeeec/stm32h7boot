/**
 * @file platform_parameters.c
 * @brief 将板级非易失存储驱动适配为参数服务端口。
 *
 * EEPROM/I2C 的初始化和读写细节留在 BSP；上层只看到通用参数存储能力。
 */

#include "platform/platform_ports.h"

#include <stddef.h>

#include "bsp/bsp_eeprom.h"

#define PLATFORM_EEPROM_ADDRESS_7BIT     0x50U
#define PLATFORM_EEPROM_WRITE_TIMEOUT_MS 10U

/** 初始化并探测参数存储设备。 */
static firmware_status_t ParameterStorageInit(void *context)
{
    const bsp_eeprom_config_t config = {
        PLATFORM_EEPROM_ADDRESS_7BIT,
        PLATFORM_EEPROM_WRITE_TIMEOUT_MS,
    };
    firmware_status_t status;

    (void) context;
    status = BSP_EepromInit(&config);
    if (FirmwareStatus_IsError(status))
        return status;
    return BSP_EepromProbe();
}

/** 参数服务的读取回调。 */
static firmware_status_t ParameterStorageRead(void *context, uint32_t address, void *data,
                                              uint32_t size)
{
    (void) context;
    return BSP_EepromRead(address, data, size);
}

/** 参数服务的写入回调。 */
static firmware_status_t ParameterStorageWrite(void *context, uint32_t address, const void *data,
                                               uint32_t size)
{
    (void) context;
    return BSP_EepromWrite(address, data, size);
}

/** 导出参数服务使用的抽象存储端口。 */
firmware_status_t Platform_GetParameterStoragePort(parameter_storage_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (parameter_storage_port_t) {
        .context        = NULL,
        .init           = ParameterStorageInit,
        .read           = ParameterStorageRead,
        .write          = ParameterStorageWrite,
        .capacity_bytes = BSP_EEPROM_CAPACITY_BYTES,
    };
    return FIRMWARE_STATUS_OK;
}