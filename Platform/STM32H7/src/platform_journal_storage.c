#include "platform/platform_journal_storage.h"

#include <string.h>

#include "stm32h7xx_hal.h"

#define PLATFORM_JOURNAL_PROGRAM_UNIT 32U

static void invalidate_flash_cache(uint32_t address, size_t size)
{
    uintptr_t aligned_address;
    size_t aligned_size;

    aligned_address = (uintptr_t) address & ~(uintptr_t) 31U;
    aligned_size = ((uintptr_t) address + size - aligned_address + 31U) & ~(uintptr_t) 31U;
    SCB_InvalidateDCache_by_Addr((volatile void *) aligned_address, (int32_t) aligned_size);
}

static uint32_t slot_address(uint32_t slot)
{
    return PLATFORM_JOURNAL_ADDRESS + slot * PLATFORM_JOURNAL_SLOT_SIZE;
}

static firmware_status_t erase_slot(uint32_t slot)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef hal_status;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_2;
    erase.Sector = FLASH_SECTOR_6 + slot;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    hal_status = HAL_FLASHEx_Erase(&erase, &sector_error);
    (void) HAL_FLASH_Lock();
    return hal_status == HAL_OK ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t PlatformJournalStorage_Init(void)
{
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformJournalStorage_Read(uint32_t slot, void *data, size_t size)
{
    if (slot >= PLATFORM_JOURNAL_SLOT_COUNT || data == NULL || size == 0U ||
        size > PLATFORM_JOURNAL_SLOT_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    invalidate_flash_cache(slot_address(slot), size);
    (void) memcpy(data, (const void *)(uintptr_t) slot_address(slot), size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformJournalStorage_Write(uint32_t slot, const void *data, size_t size)
{
    static uint8_t program_buffer[PLATFORM_JOURNAL_PROGRAM_UNIT]
        __attribute__((aligned(32)));
    const uint8_t *source = (const uint8_t *) data;
    size_t remaining = size;
    uint32_t address;
    firmware_status_t status;

    if (slot >= PLATFORM_JOURNAL_SLOT_COUNT || data == NULL || size == 0U ||
        size > PLATFORM_JOURNAL_SLOT_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    status = erase_slot(slot);
    if (FirmwareStatus_IsError(status))
        return status;

    address = slot_address(slot);
    status = FIRMWARE_STATUS_OK;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    while (remaining != 0U)
    {
        size_t chunk = remaining < sizeof(program_buffer) ? remaining : sizeof(program_buffer);
        HAL_StatusTypeDef hal_status;

        (void) memset(program_buffer, 0xFF, sizeof(program_buffer));
        (void) memcpy(program_buffer, source, chunk);
        SCB_CleanDCache_by_Addr((volatile void *) program_buffer,
                                (int32_t) sizeof(program_buffer));
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                                       (uint32_t)(uintptr_t) program_buffer);
        if (hal_status != HAL_OK)
        {
            status = FIRMWARE_STATUS_IO_ERROR;
            break;
        }
        address += sizeof(program_buffer);
        source += chunk;
        remaining -= chunk;
    }
    (void) HAL_FLASH_Lock();
    return status;
}
