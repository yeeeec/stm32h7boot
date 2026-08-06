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

static firmware_status_t Enter(void *context)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef configuration;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->mapped != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

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
        (QSPI_HandleTypeDef *)adapter->qspi_handle,
        &command,
        &configuration));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->mapped = 1;
    }
    return status;
}

static firmware_status_t Exit(void *context)
{
    stm32_qspi_xip_adapter_t *adapter =
        (stm32_qspi_xip_adapter_t *)context;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->mapped == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = HalStatus(HAL_QSPI_Abort(
        (QSPI_HandleTypeDef *)adapter->qspi_handle));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->mapped = 0;
    }
    return status;
}

static firmware_status_t IsMapped(void *context, int *mapped)
{
    const stm32_qspi_xip_adapter_t *adapter =
        (const stm32_qspi_xip_adapter_t *)context;

    if ((adapter == NULL) || (mapped == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *mapped = adapter->mapped;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Invalidate(
    void *context,
    uint32_t mapped_address,
    uint32_t size)
{
    const stm32_qspi_xip_adapter_t *adapter =
        (const stm32_qspi_xip_adapter_t *)context;
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
