/**
 * @file at24_boot_control_adapter.c
 * @brief 基于 AT24 驱动实现 Boot Control 存储操作。
 */
#include "adapters/at24_boot_control_adapter.h"

#include <stddef.h>

#include "at24.h"

/** 将 AT24 容量和页大小转换为 Boot Control 存储信息。 */
static firmware_status_t GetInfo(void *context, boot_control_store_info_t *info)
{
    at24_info_t device_info;
    firmware_status_t status;

    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = At24_GetInfo((const at24_t *) context, &device_info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    info->capacity_bytes = device_info.capacity_bytes;
    info->page_size      = device_info.page_size;
    return FIRMWARE_STATUS_OK;
}

/** 从 AT24 读取指定地址范围的数据。 */
static firmware_status_t Read(void *context, uint32_t address, void *data, uint32_t size)
{
    return At24_Read((at24_t *) context, address, data, size);
}

/** 启动一次符合 EEPROM 页边界的异步分页写入。 */
static firmware_status_t WritePage(void *context, uint32_t address, const void *data, uint32_t size)
{
    return At24_WritePageStart((at24_t *) context, address, data, size);
}

/** 轮询 AT24 写操作，并将驱动状态转换为 ready 标志。 */
static firmware_status_t IsReady(void *context, int *ready)
{
    at24_operation_result_t result;
    firmware_status_t status;

    if (ready == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 先推进底层状态机，再读取本次操作的最终状态。 */
    status = At24_OperationPoll((at24_t *) context);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = At24_GetOperationResult((const at24_t *) context, &result);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (result.state == AT24_OPERATION_FAILED)
    {
        return result.status;
    }
    *ready = (result.state == AT24_OPERATION_BUSY) ? 0 : 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t At24BootControlAdapter_Init(at24_boot_control_adapter_t *adapter,
                                              struct at24 *device)
{
    at24_info_t info;
    firmware_status_t status;

    if ((adapter == NULL) || (device == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 查询设备可用性，确保适配器不会绑定未初始化的驱动对象。 */
    status = At24_GetInfo((const at24_t *) device, &info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    (void) info;

    /* 回调表只保存设备引用，不接管设备的生命周期。 */
    adapter->device               = device;
    adapter->interface.context    = device;
    adapter->interface.get_info   = GetInfo;
    adapter->interface.read       = Read;
    adapter->interface.write_page = WritePage;
    adapter->interface.is_ready   = IsReady;
    return FIRMWARE_STATUS_OK;
}

const boot_control_store_t *
At24BootControlAdapter_Interface(const at24_boot_control_adapter_t *adapter)
{
    /* 接口内嵌在适配器中，生命周期与适配器一致。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
