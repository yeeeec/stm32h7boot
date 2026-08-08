/**
 * @file stm32_qspi_xip_adapter.c
 * @brief STM32 QSPI memory-mapped execution control implementation.
 */
#include "adapters/stm32_qspi_xip_adapter.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/platform_cache.h"
#include "stm32h7xx_hal.h"

#define QSPI_FAST_READ_INSTRUCTION 0x0BU
#define QSPI_FAST_READ_DUMMY_CYCLES 8U
#define QSPI_MAPPED_BASE            0x90000000UL
#define QSPI_MAPPED_SIZE            (32UL * 1024UL * 1024UL)

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

/**
 * @brief 直接读取 QSPI CCR 的功能模式位，作为 memory-mapped 状态的硬件事实来源。
 *
 * HAL Handle 的 State 字段是软件缓存；调试器、复位恢复或超时 Abort 后，它可能与
 * 外设寄存器不同步。服务层的安全决策必须依赖实际 CCR.FMODE，而非该缓存。
 */
static int HardwareIsMemoryMapped(const QSPI_HandleTypeDef *handle)
{
    return (handle != NULL) && (handle->Instance != NULL) &&
           ((READ_BIT(handle->Instance->CCR, QUADSPI_CCR_FMODE) == QUADSPI_CCR_FMODE) ? 1 : 0);
}

/**
 * @brief 在外设已空闲且寄存器确认 indirect 时，修复遗留的 HAL memory-mapped 状态。
 *
 * HAL_QSPI_MemoryMapped 在等待空闲超时时，CCR 可能仍为 indirect，而 HAL 状态会
 * 遗留为 BUSY_MEM_MAPPED 或带纯 TIMEOUT 原因的 ERROR。若不恢复为 READY，之后
 * 所有 indirect 命令都会返回 HAL_BUSY。这里只修复这两种可由硬件事实证明安全的
 * 残留，绝不覆盖真实的 indirect 传输、DMA 或传输错误。
 */
static void SynchronizeIndirectHalState(QSPI_HandleTypeDef *handle)
{
    if ((handle == NULL) || (handle->Instance == NULL) ||
        (HardwareIsMemoryMapped(handle) != 0) ||
        (READ_BIT(handle->Instance->SR, QSPI_FLAG_BUSY) != 0U))
    {
        return;
    }
    if ((handle->State == HAL_QSPI_STATE_BUSY_MEM_MAPPED) ||
        ((handle->State == HAL_QSPI_STATE_ERROR) &&
         (handle->ErrorCode == HAL_QSPI_ERROR_TIMEOUT)))
    {
        handle->State = HAL_QSPI_STATE_READY;
    }
}

static firmware_status_t Enter(void *context)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    QSPI_HandleTypeDef *handle;
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef configuration;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    handle = (QSPI_HandleTypeDef *)adapter->qspi_handle;
    if ((handle == NULL) || (handle->Instance == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    adapter->mapped = HardwareIsMemoryMapped(handle);
    if (adapter->mapped != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    SynchronizeIndirectHalState(handle);

    memset(&command, 0, sizeof(command));
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.Instruction = QSPI_FAST_READ_INSTRUCTION;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_32_BITS;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.DummyCycles = QSPI_FAST_READ_DUMMY_CYCLES;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    configuration.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    configuration.TimeOutPeriod = 0U;

    status = HalStatus(HAL_QSPI_MemoryMapped(
        handle,
        &command,
        &configuration));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->mapped = HardwareIsMemoryMapped(handle);
        if (adapter->mapped == 0)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    return status;
}

static firmware_status_t Exit(void *context)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    QSPI_HandleTypeDef *handle;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    handle = (QSPI_HandleTypeDef *)adapter->qspi_handle;
    if ((handle == NULL) || (handle->Instance == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    adapter->mapped = HardwareIsMemoryMapped(handle);
    if (adapter->mapped == 0)
    {
        SynchronizeIndirectHalState(handle);
        return FIRMWARE_STATUS_OK;
    }
    /*
     * 外部调试器或异常复位可能只保留硬件 CCR，未同步 HAL State。让 HAL Abort
     * 进入正确分支后再执行；本 Adapter 是该 Handle 的唯一模式所有者。
     */
    if (HAL_QSPI_GetState(handle) != HAL_QSPI_STATE_BUSY_MEM_MAPPED)
    {
        handle->State = HAL_QSPI_STATE_BUSY_MEM_MAPPED;
    }
    status = HalStatus(HAL_QSPI_Abort(
        handle));
    if (FirmwareStatus_IsOk(status))
    {
        /* HAL 在空闲 memory-mapped 窗口上可能不清 FMODE；成功 Abort 后补齐后置条件。 */
        CLEAR_BIT(handle->Instance->CCR, QUADSPI_CCR_FMODE);
    }
    adapter->mapped = HardwareIsMemoryMapped(handle);
    if (adapter->mapped == 0)
    {
        SynchronizeIndirectHalState(handle);
    }
    else if (FirmwareStatus_IsOk(status))
    {
        /* 成功返回必须同时满足已离开 memory-mapped 窗口这一后置条件。 */
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return status;
}

static firmware_status_t IsMapped(void *context, int *mapped)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    QSPI_HandleTypeDef *handle;

    if ((adapter == NULL) || (mapped == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    handle = (QSPI_HandleTypeDef *)adapter->qspi_handle;
    if ((handle == NULL) || (handle->Instance == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    adapter->mapped = HardwareIsMemoryMapped(handle);
    if (adapter->mapped == 0)
    {
        SynchronizeIndirectHalState(handle);
    }
    *mapped = adapter->mapped;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Invalidate(
    void *context,
    uint32_t mapped_address,
    uint32_t size)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    QSPI_HandleTypeDef *handle;
    uint32_t range_end;
    uint32_t aligned_start;
    uint32_t aligned_end;
    firmware_status_t status;

    if ((adapter == NULL) || (size == 0U) ||
        (mapped_address < QSPI_MAPPED_BASE) ||
        (mapped_address > UINT32_MAX - size))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    handle = (QSPI_HandleTypeDef *)adapter->qspi_handle;
    if ((handle == NULL) || (handle->Instance == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    adapter->mapped = HardwareIsMemoryMapped(handle);
    if (adapter->mapped == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    range_end = mapped_address + size;
    if (range_end > (QSPI_MAPPED_BASE + QSPI_MAPPED_SIZE))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    aligned_start = mapped_address & ~(PLATFORM_DCACHE_LINE_SIZE - 1U);
    if (range_end > UINT32_MAX - (PLATFORM_DCACHE_LINE_SIZE - 1U))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    aligned_end = (range_end + PLATFORM_DCACHE_LINE_SIZE - 1U) &
                  ~(PLATFORM_DCACHE_LINE_SIZE - 1U);
    status = Platform_DCacheInvalidate(
        (const void *)(uintptr_t)aligned_start,
        (size_t)(aligned_end - aligned_start));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    SCB_InvalidateICache();
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32QspiXipAdapter_Init(
    stm32_qspi_xip_adapter_t *adapter,
    void *qspi_handle)
{
    if ((adapter == NULL) || (qspi_handle == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    adapter->qspi_handle = qspi_handle;
    adapter->mapped = 0;
    adapter->interface.context = adapter;
    adapter->interface.enter_memory_mapped_read = Enter;
    adapter->interface.exit_memory_mapped = Exit;
    adapter->interface.is_memory_mapped = IsMapped;
    adapter->interface.invalidate_mapped_cache = Invalidate;
    return FIRMWARE_STATUS_OK;
}

const xip_controller_t *Stm32QspiXipAdapter_Interface(
    const stm32_qspi_xip_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
