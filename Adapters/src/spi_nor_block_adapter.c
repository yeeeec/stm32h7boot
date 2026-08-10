/**
 * @file spi_nor_block_adapter.c
 * @brief 基于 SPI NOR 设备实现通用及异步块设备操作。
 */
#include "adapters/spi_nor_block_adapter.h"

#include <stddef.h>

#include "spi_nor.h"

/** 查询 SPI NOR 参数，并填充同步块设备信息。 */
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

/** 将通用读取请求转发给 SPI NOR 驱动。 */
static firmware_status_t Read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    return SpiNor_Read((spi_nor_t *)context, address, data, size);
}

/** 将同步编程请求转发给 SPI NOR 驱动。 */
static firmware_status_t Program(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    return SpiNor_Program((spi_nor_t *)context, address, data, size);
}

/** 将同步擦除请求转发给 SPI NOR 驱动。 */
static firmware_status_t Erase(
    void *context,
    uint32_t address,
    uint32_t size)
{
    return SpiNor_Erase((spi_nor_t *)context, address, size);
}

/** 查询 SPI NOR 参数，并填充异步块设备信息。 */
static firmware_status_t AsyncGetInfo(
    void *context,
    async_block_device_info_t *info)
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
    info->program_size = device_info.page_size;
    info->erase_size = device_info.erase_size;
    return FIRMWARE_STATUS_OK;
}

/** 启动一次异步擦除操作。 */
static firmware_status_t EraseStart(
    void *context,
    uint32_t address,
    uint32_t size)
{
    return SpiNor_EraseStart((spi_nor_t *)context, address, size);
}

/** 校验页边界后启动一次异步编程操作。 */
static firmware_status_t AsyncProgram(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    spi_nor_info_t info;
    firmware_status_t status = SpiNor_GetInfo((spi_nor_t *)context, &info);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* 单次编程不能跨越 SPI NOR 的页边界。 */
    if ((size == 0U) || (size > info.page_size) ||
        ((address % info.page_size) > (info.page_size - size)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    return SpiNor_ProgramStart((spi_nor_t *)context, address, data, size);
}

/** 推进 SPI NOR 异步操作状态机。 */
static firmware_status_t Poll(void *context)
{
    return SpiNor_OperationPoll((spi_nor_t *)context);
}

/** 将 SPI NOR 操作状态映射为通用异步块设备状态。 */
static firmware_status_t GetOperationResult(
    void *context,
    async_block_device_operation_result_t *result)
{
    spi_nor_operation_result_t driver_result;
    firmware_status_t status;

    if (result == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = SpiNor_GetOperationResult((spi_nor_t *)context, &driver_result);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    switch (driver_result.state)
    {
        case SPI_NOR_OPERATION_IDLE:
            result->state = ASYNC_BLOCK_DEVICE_OPERATION_IDLE;
            break;
        case SPI_NOR_OPERATION_BUSY:
            result->state = ASYNC_BLOCK_DEVICE_OPERATION_BUSY;
            break;
        case SPI_NOR_OPERATION_SUCCEEDED:
            result->state = ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED;
            break;
        default:
            result->state = ASYNC_BLOCK_DEVICE_OPERATION_FAILED;
            break;
    }
    result->status = driver_result.status;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t SpiNorBlockAdapter_Init(
    spi_nor_block_adapter_t *adapter,
    struct spi_nor *device)
{
    if ((adapter == NULL) || (device == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    /* 同时初始化同步和异步接口；两个接口共享同一个驱动上下文。 */
    adapter->device = device;
    adapter->interface.context = device;
    adapter->interface.get_info = GetInfo;
    adapter->interface.read = Read;
    adapter->interface.program = Program;
    adapter->interface.erase = Erase;
    adapter->async_interface.context = device;
    adapter->async_interface.get_info = AsyncGetInfo;
    adapter->async_interface.read = Read;
    adapter->async_interface.program_start = AsyncProgram;
    adapter->async_interface.erase_start = EraseStart;
    adapter->async_interface.poll = Poll;
    adapter->async_interface.get_operation_result = GetOperationResult;
    adapter->async_interface.cancel = NULL;
    return FIRMWARE_STATUS_OK;
}

const async_block_device_t *SpiNorBlockAdapter_AsyncInterface(
    const spi_nor_block_adapter_t *adapter)
{
    /* 返回适配器内嵌的异步接口。 */
    return (adapter == NULL) ? NULL : &adapter->async_interface;
}

const block_device_t *SpiNorBlockAdapter_Interface(
    const spi_nor_block_adapter_t *adapter)
{
    /* 返回适配器内嵌的同步接口。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
