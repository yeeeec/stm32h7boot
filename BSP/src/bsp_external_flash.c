/**
 * @file bsp_external_flash.c
 * @brief 可移植 SPI NOR 驱动的板级 QSPI 传输实现。
 *
 * 驱动负责 JEDEC 探测、地址模式选择、几何参数和操作状态。本模块持有静态
 * 设备实例，并将每个驱动事务转换为保守的单线 STM32 QSPI 命令。本模块不负责
 * XIP/memory-mapped 状态切换；该职责属于 Launch 和 Update Service 使用的专用
 * XIP Adapter。
 */
#include "bsp/bsp_external_flash.h"

#include <stddef.h>
#include <string.h>

#include "spi_nor.h"
#include "quadspi.h"

#define BSP_QSPI_COMMAND_TIMEOUT_MS 100U
#define BSP_QSPI_MAX_TRANSFER_SIZE  65535U

static spi_nor_t external_flash;

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

static firmware_status_t PrepareCommand(
    const spi_nor_transaction_t *transaction,
    uint32_t data_size,
    QSPI_CommandTypeDef *command)
{
    if ((transaction == NULL) || (command == NULL) ||
        (transaction->address_bytes > 4U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Command、Address、Data 阶段保持 1-1-1 mode。更快的板级模式需要协调
     * Driver Contract，不能在此处自行推断。
     */
    memset(command, 0, sizeof(*command));
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->Instruction = transaction->instruction;
    command->AddressMode = (transaction->address_bytes == 0U)
                               ? QSPI_ADDRESS_NONE
                               : QSPI_ADDRESS_1_LINE;
    command->Address = transaction->address;
    command->AddressSize = (transaction->address_bytes == 4U)
                               ? QSPI_ADDRESS_32_BITS
                               : QSPI_ADDRESS_24_BITS;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DataMode = (data_size == 0U) ? QSPI_DATA_NONE : QSPI_DATA_1_LINE;
    command->NbData = data_size;
    command->DummyCycles = transaction->dummy_cycles;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t QspiCommand(
    void *context,
    const spi_nor_transaction_t *transaction)
{
    QSPI_HandleTypeDef *handle = (QSPI_HandleTypeDef *)context;
    QSPI_CommandTypeDef command;
    firmware_status_t status = PrepareCommand(transaction, 0U, &command);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return HalStatus(HAL_QSPI_Command(
        handle, &command, BSP_QSPI_COMMAND_TIMEOUT_MS));
}

static firmware_status_t QspiReceive(
    void *context,
    const spi_nor_transaction_t *transaction,
    uint8_t *data,
    uint32_t size)
{
    QSPI_HandleTypeDef *handle = (QSPI_HandleTypeDef *)context;
    QSPI_CommandTypeDef command;
    firmware_status_t status;

    if ((data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = PrepareCommand(transaction, size, &command);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = HalStatus(HAL_QSPI_Command(
        handle, &command, BSP_QSPI_COMMAND_TIMEOUT_MS));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    return HalStatus(HAL_QSPI_Receive(
        handle, data, BSP_QSPI_COMMAND_TIMEOUT_MS));
}

static firmware_status_t QspiTransmit(
    void *context,
    const spi_nor_transaction_t *transaction,
    const uint8_t *data,
    uint32_t size)
{
    QSPI_HandleTypeDef *handle = (QSPI_HandleTypeDef *)context;
    QSPI_CommandTypeDef command;
    firmware_status_t status;

    if ((data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = PrepareCommand(transaction, size, &command);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = HalStatus(HAL_QSPI_Command(
        handle, &command, BSP_QSPI_COMMAND_TIMEOUT_MS));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    return HalStatus(HAL_QSPI_Transmit(
        handle, (uint8_t *)data, BSP_QSPI_COMMAND_TIMEOUT_MS));
}

static uint32_t QspiNowMs(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static void QspiDelayMs(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

firmware_status_t BSP_ExternalFlashInit(void)
{
    spi_nor_port_t port;
    /* 零值选择可移植驱动定义的权威 Timeout 默认值。 */
    spi_nor_config_t config = {0};

    if (hqspi.State == HAL_QSPI_STATE_RESET)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    port.context = &hqspi;
    port.command = QspiCommand;
    port.receive = QspiReceive;
    port.transmit = QspiTransmit;
    port.now_ms = QspiNowMs;
    port.delay_ms = QspiDelayMs;
    port.poll_hook = NULL;
    port.max_transfer_size = BSP_QSPI_MAX_TRANSFER_SIZE;

    /* SpiNor_Init 会先校验 JEDEC Identity，再使对象对外可见。 */
    return SpiNor_Init(&external_flash, &port, &config);
}

struct spi_nor *BSP_ExternalFlashDevice(void)
{
    return (external_flash.initialized != 0) ? &external_flash : NULL;
}
