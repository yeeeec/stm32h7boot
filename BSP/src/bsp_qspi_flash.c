#include "bsp/bsp_qspi_flash.h"

#include <stddef.h>

#include "quadspi.h"

#define BSP_QSPI_FLASH_TIMEOUT_MS 1000U

static uint8_t s_initialized;
static uint8_t s_memory_mapped;

static firmware_status_t BspQspiFlash_MapHalStatus(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:
            return FIRMWARE_STATUS_OK;

        case HAL_BUSY:
            return FIRMWARE_STATUS_BUSY;

        case HAL_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;

        case HAL_ERROR:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static firmware_status_t BspQspiFlash_BuildCommand(const bsp_qspi_flash_transaction_t *transaction,
                                                   uint32_t data_mode, uint32_t data_size,
                                                   QSPI_CommandTypeDef *command)
{
    if ((transaction == NULL) || (command == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    if ((transaction->address_bytes != 0U) && (transaction->address_bytes != 3U) &&
        (transaction->address_bytes != 4U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    *command                   = (QSPI_CommandTypeDef) {0};
    command->InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    command->Instruction       = transaction->instruction;
    command->AddressMode       = QSPI_ADDRESS_NONE;
    command->AddressSize       = QSPI_ADDRESS_8_BITS;
    command->Address           = transaction->address;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DataMode          = data_mode;
    command->DummyCycles       = transaction->dummy_cycles;
    command->NbData            = data_size;
    command->DdrMode           = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (transaction->address_bytes == 3U)
    {
        command->AddressMode = QSPI_ADDRESS_1_LINE;
        command->AddressSize = QSPI_ADDRESS_24_BITS;
    }
    else if (transaction->address_bytes == 4U)
    {
        command->AddressMode = QSPI_ADDRESS_1_LINE;
        command->AddressSize = QSPI_ADDRESS_32_BITS;
    }

    return FIRMWARE_STATUS_OK;
}

firmware_status_t BspQspiFlash_Init(void)
{
    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    MX_QUADSPI_Init();
    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t
BspQspiFlash_EnterMemoryMapped(const bsp_qspi_flash_transaction_t *read_transaction)
{
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef memory_mapped = {0};
    firmware_status_t status;

    if (read_transaction == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = BspQspiFlash_BuildCommand(read_transaction, QSPI_DATA_1_LINE, 0U, &command);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    memory_mapped.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    memory_mapped.TimeOutPeriod     = 0U;
    status = BspQspiFlash_MapHalStatus(HAL_QSPI_MemoryMapped(&hqspi, &command, &memory_mapped));
    if (status == FIRMWARE_STATUS_OK)
    {
        s_memory_mapped = 1U;
    }
    return status;
}

firmware_status_t BspQspiFlash_ExitMemoryMapped(void)
{
    firmware_status_t status;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped == 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = BspQspiFlash_MapHalStatus(HAL_QSPI_Abort(&hqspi));
    if (status == FIRMWARE_STATUS_OK)
    {
        s_memory_mapped = 0U;
    }
    return status;
}

firmware_status_t BspQspiFlash_Command(const bsp_qspi_flash_transaction_t *transaction)
{
    QSPI_CommandTypeDef command;
    firmware_status_t status;

    if ((s_initialized == 0U) || (s_memory_mapped != 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BspQspiFlash_BuildCommand(transaction, QSPI_DATA_NONE, 0U, &command);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    return BspQspiFlash_MapHalStatus(HAL_QSPI_Command(&hqspi, &command, BSP_QSPI_FLASH_TIMEOUT_MS));
}

firmware_status_t BspQspiFlash_Receive(const bsp_qspi_flash_transaction_t *transaction,
                                       uint8_t *data, uint32_t size)
{
    QSPI_CommandTypeDef command;
    firmware_status_t status;

    if ((data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    if (s_initialized == 0U || s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BspQspiFlash_BuildCommand(transaction, QSPI_DATA_1_LINE, size, &command);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    status =
        BspQspiFlash_MapHalStatus(HAL_QSPI_Command(&hqspi, &command, BSP_QSPI_FLASH_TIMEOUT_MS));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    return BspQspiFlash_MapHalStatus(HAL_QSPI_Receive(&hqspi, data, BSP_QSPI_FLASH_TIMEOUT_MS));
}

firmware_status_t BspQspiFlash_Transmit(const bsp_qspi_flash_transaction_t *transaction,
                                        const uint8_t *data, uint32_t size)
{
    QSPI_CommandTypeDef command;
    firmware_status_t status;

    if ((data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    if (s_initialized == 0U || s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BspQspiFlash_BuildCommand(transaction, QSPI_DATA_1_LINE, size, &command);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    status =
        BspQspiFlash_MapHalStatus(HAL_QSPI_Command(&hqspi, &command, BSP_QSPI_FLASH_TIMEOUT_MS));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    return BspQspiFlash_MapHalStatus(
        HAL_QSPI_Transmit(&hqspi, (uint8_t *) data, BSP_QSPI_FLASH_TIMEOUT_MS));
}
