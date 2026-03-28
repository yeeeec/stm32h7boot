#include "boot_extflash.h"

#include <string.h>

#include "boot_config.h"
#include "boot_log.h"
#include "platform/boot_platform.h"
#include "platform/qspi.h"
#include "stm32h7xx_hal.h"

#define BOOT_EXTFLASH_CMD_READ_STATUS   0x05U
#define BOOT_EXTFLASH_CMD_WRITE_ENABLE  0x06U
#define BOOT_EXTFLASH_CMD_READ_DATA     0x03U
#define BOOT_EXTFLASH_CMD_PAGE_PROGRAM  0x02U
#define BOOT_EXTFLASH_CMD_SECTOR_ERASE  0x20U

#define BOOT_EXTFLASH_STATUS_WIP        0x01U
#define BOOT_EXTFLASH_STATUS_WEL        0x02U
#define BOOT_EXTFLASH_STATUS_POLL_INTERVAL 0x10U

static uint8_t g_boot_extflash_initialized;
static uint8_t g_boot_extflash_memory_mapped;

static BootError Boot_ExtFlash_CommandError(BootError error, Plat_Status_t status) {
    return (status == PLAT_OK) ? BOOT_ERR_NONE : error;
}

bool Boot_ExtFlash_IsRangeValid(uint32_t address, uint32_t size) {
    uint32_t end_address;

    if ((size == 0U) || (address < BOOT_EXTFLASH_BASE)) {
        return false;
    }

    end_address = address + size;
    if ((end_address < address) || (end_address > BOOT_EXTFLASH_END)) {
        return false;
    }

    return true;
}

static uint32_t Boot_ExtFlash_AddressToOffset(uint32_t address) {
    return address - BOOT_EXTFLASH_BASE + BOOT_EXTFLASH_ALLOWED_OFFSET;
}

static void Boot_ExtFlash_InvalidateCaches(void) {
#if defined(SCB_CCR_DC_Msk)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanInvalidateDCache();
    }
#endif
#if defined(SCB_CCR_IC_Msk)
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U) {
        SCB_InvalidateICache();
    }
#endif
    __DSB();
    __ISB();
}

static BootError Boot_ExtFlash_AbortMemoryMapped(void) {
    HAL_StatusTypeDef hal_status;

    if (g_boot_extflash_memory_mapped == 0U) {
        return BOOT_ERR_NONE;
    }

    hal_status = HAL_QSPI_Abort(platform_qspi_get_handle());
    if (hal_status != HAL_OK) {
        return BOOT_ERR_EXTFLASH_READ;
    }

    g_boot_extflash_memory_mapped = 0U;
    return BOOT_ERR_NONE;
}

static BootError Boot_ExtFlash_EnableMemoryMapped(void) {
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef memory_mapped;
    BootError error;

    if (g_boot_extflash_memory_mapped != 0U) {
        return BOOT_ERR_NONE;
    }

    error = Boot_ExtFlash_AbortMemoryMapped();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    memset(&command, 0, sizeof(command));
    command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    command.Instruction       = BOOT_EXTFLASH_CMD_READ_DATA;
    command.AddressMode       = QSPI_ADDRESS_1_LINE;
    command.AddressSize       = QSPI_ADDRESS_24_BITS;
    command.Address           = 0U;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode          = QSPI_DATA_1_LINE;
    command.DummyCycles       = 0U;
    command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    memset(&memory_mapped, 0, sizeof(memory_mapped));
    memory_mapped.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    memory_mapped.TimeOutPeriod     = 0U;

    error = Boot_ExtFlash_CommandError(
        BOOT_ERR_EXTFLASH_READ,
        platform_qspi_memory_mapped(&command, &memory_mapped));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    g_boot_extflash_memory_mapped = 1U;
    Boot_ExtFlash_InvalidateCaches();
    return BOOT_ERR_NONE;
}

static BootError Boot_ExtFlash_SendSimpleCommand(uint8_t instruction) {
    QSPI_CommandTypeDef command;

    memset(&command, 0, sizeof(command));
    command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    command.Instruction       = instruction;
    command.AddressMode       = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode          = QSPI_DATA_NONE;
    command.DummyCycles       = 0U;
    command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    return Boot_ExtFlash_CommandError(BOOT_ERR_EXTFLASH_WRITE,
                                      platform_qspi_command(&command, BOOT_EXTFLASH_CMD_TIMEOUT_MS));
}

static void Boot_ExtFlash_BuildStatusCommand(QSPI_CommandTypeDef *command) {
    memset(command, 0, sizeof(*command));
    command->InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    command->Instruction       = BOOT_EXTFLASH_CMD_READ_STATUS;
    command->AddressMode       = QSPI_ADDRESS_NONE;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DataMode          = QSPI_DATA_1_LINE;
    command->NbData            = 1U;
    command->DummyCycles       = 0U;
    command->DdrMode           = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
}

static BootError Boot_ExtFlash_AutoPollStatus(uint8_t match,
                                              uint8_t mask,
                                              uint32_t timeout_ms,
                                              BootError error) {
    QSPI_CommandTypeDef command;
    QSPI_AutoPollingTypeDef polling;
    Plat_Status_t status;

    Boot_ExtFlash_BuildStatusCommand(&command);

    memset(&polling, 0, sizeof(polling));
    polling.Match           = match;
    polling.Mask            = mask;
    polling.MatchMode       = QSPI_MATCH_MODE_AND;
    polling.StatusBytesSize = 1U;
    polling.Interval        = BOOT_EXTFLASH_STATUS_POLL_INTERVAL;
    polling.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    status = platform_qspi_auto_polling(&command, &polling, timeout_ms);
    if (status != PLAT_OK) {
        LOG_WARN(BOOT_LOG_TAG,
                 "ExtFlash status poll failed: match=0x%02X mask=0x%02X timeout=%lu status=%ld",
                 (unsigned int) match,
                 (unsigned int) mask,
                 (unsigned long) timeout_ms,
                 (long) status);
    }

    return Boot_ExtFlash_CommandError(error, status);
}

static BootError Boot_ExtFlash_WaitReady(uint32_t timeout_ms, BootError error) {
    return Boot_ExtFlash_AutoPollStatus(0U, BOOT_EXTFLASH_STATUS_WIP, timeout_ms, error);
}

static BootError Boot_ExtFlash_WriteEnable(void) {
    BootError error;

    error = Boot_ExtFlash_SendSimpleCommand(BOOT_EXTFLASH_CMD_WRITE_ENABLE);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    return Boot_ExtFlash_AutoPollStatus(BOOT_EXTFLASH_STATUS_WEL,
                                        BOOT_EXTFLASH_STATUS_WEL,
                                        BOOT_EXTFLASH_CMD_TIMEOUT_MS,
                                        BOOT_ERR_EXTFLASH_WRITE);
}

static BootError Boot_ExtFlash_EnsureReady(void) {
    BootError error;

    if (platform_qspi_is_ready() == false) {
        return BOOT_ERR_EXTFLASH_READ;
    }

    if (g_boot_extflash_initialized == 0U) {
        error = Boot_ExtFlash_EnableMemoryMapped();
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        g_boot_extflash_initialized = 1U;
    }

    return BOOT_ERR_NONE;
}

BootError Boot_ExtFlash_Init(void) {
    return Boot_ExtFlash_EnsureReady();
}

BootError Boot_ExtFlash_Read(uint32_t address, void *buffer, uint32_t size) {
    BootError error;

    if ((buffer == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_ExtFlash_IsRangeValid(address, size) == false) {
        return BOOT_ERR_EXTFLASH_RANGE;
    }

    error = Boot_ExtFlash_EnsureReady();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_ExtFlash_EnableMemoryMapped();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    memcpy(buffer, (const void *)(uintptr_t) address, size);
    return BOOT_ERR_NONE;
}

BootError Boot_ExtFlash_Write(uint32_t address, const void *data, uint32_t size) {
    const uint8_t *source;
    uint32_t write_address;
    uint32_t remaining;
    BootError error;

    if ((data == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_ExtFlash_IsRangeValid(address, size) == false) {
        return BOOT_ERR_EXTFLASH_RANGE;
    }

    error = Boot_ExtFlash_EnsureReady();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_ExtFlash_AbortMemoryMapped();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    source        = (const uint8_t *) data;
    write_address = address;
    remaining     = size;
    while (remaining > 0U) {
        QSPI_CommandTypeDef command;
        uint32_t page_offset;
        uint32_t chunk_size;

        page_offset = Boot_ExtFlash_AddressToOffset(write_address) % BOOT_EXTFLASH_PAGE_SIZE;
        chunk_size  = BOOT_EXTFLASH_PAGE_SIZE - page_offset;
        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        error = Boot_ExtFlash_WriteEnable();
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        memset(&command, 0, sizeof(command));
        command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
        command.Instruction       = BOOT_EXTFLASH_CMD_PAGE_PROGRAM;
        command.AddressMode       = QSPI_ADDRESS_1_LINE;
        command.AddressSize       = QSPI_ADDRESS_24_BITS;
        command.Address           = Boot_ExtFlash_AddressToOffset(write_address);
        command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
        command.DataMode          = QSPI_DATA_1_LINE;
        command.NbData            = chunk_size;
        command.DummyCycles       = 0U;
        command.DdrMode           = QSPI_DDR_MODE_DISABLE;
        command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
        command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

        error = Boot_ExtFlash_CommandError(
            BOOT_ERR_EXTFLASH_WRITE,
            platform_qspi_command(&command, BOOT_EXTFLASH_CMD_TIMEOUT_MS));
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        error = Boot_ExtFlash_CommandError(
            BOOT_ERR_EXTFLASH_WRITE,
            platform_qspi_transmit((uint8_t *) source, BOOT_EXTFLASH_WRITE_TIMEOUT_MS));
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        error = Boot_ExtFlash_WaitReady(BOOT_EXTFLASH_WRITE_TIMEOUT_MS, BOOT_ERR_EXTFLASH_WRITE);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        write_address += chunk_size;
        source += chunk_size;
        remaining -= chunk_size;
        Boot_Platform_FeedWatchdog();
    }

    return Boot_ExtFlash_EnableMemoryMapped();
}

BootError Boot_ExtFlash_Erase(uint32_t address, uint32_t size) {
    uint32_t current_address;
    uint32_t end_address;
    BootError error;

    if ((size == 0U) || (Boot_ExtFlash_IsRangeValid(address, size) == false)) {
        return BOOT_ERR_EXTFLASH_RANGE;
    }

    if (((address % BOOT_EXTFLASH_SECTOR_SIZE) != 0U) ||
        ((size % BOOT_EXTFLASH_SECTOR_SIZE) != 0U)) {
        return BOOT_ERR_EXTFLASH_RANGE;
    }

    error = Boot_ExtFlash_EnsureReady();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_ExtFlash_AbortMemoryMapped();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    current_address = address;
    end_address     = address + size;
    while (current_address < end_address) {
        QSPI_CommandTypeDef command;

        error = Boot_ExtFlash_WriteEnable();
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        memset(&command, 0, sizeof(command));
        command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
        command.Instruction       = BOOT_EXTFLASH_CMD_SECTOR_ERASE;
        command.AddressMode       = QSPI_ADDRESS_1_LINE;
        command.AddressSize       = QSPI_ADDRESS_24_BITS;
        command.Address           = Boot_ExtFlash_AddressToOffset(current_address);
        command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
        command.DataMode          = QSPI_DATA_NONE;
        command.DummyCycles       = 0U;
        command.DdrMode           = QSPI_DDR_MODE_DISABLE;
        command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
        command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

        error = Boot_ExtFlash_CommandError(
            BOOT_ERR_EXTFLASH_ERASE,
            platform_qspi_command(&command, BOOT_EXTFLASH_CMD_TIMEOUT_MS));
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        error = Boot_ExtFlash_WaitReady(BOOT_EXTFLASH_ERASE_TIMEOUT_MS, BOOT_ERR_EXTFLASH_ERASE);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        current_address += BOOT_EXTFLASH_SECTOR_SIZE;
        Boot_Platform_FeedWatchdog();
    }

    return Boot_ExtFlash_EnableMemoryMapped();
}
