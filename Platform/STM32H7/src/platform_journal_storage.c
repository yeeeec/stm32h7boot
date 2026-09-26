#include "platform/platform_journal_storage.h"

#include <string.h>

#include "stm32h7xx_hal.h"

#define PLATFORM_JOURNAL_SLOT0_ADDRESS 0x081C0000UL
#define PLATFORM_JOURNAL_SLOT1_ADDRESS 0x081E0000UL
#define PLATFORM_JOURNAL_PROGRAM_UNIT  32U

static uint32_t slot_address(uint32_t slot)
{
    return slot == 0U ? PLATFORM_JOURNAL_SLOT0_ADDRESS : PLATFORM_JOURNAL_SLOT1_ADDRESS;
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

    (void) memcpy(data, (const void *)(uintptr_t)slot_address(slot), size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformJournalStorage_Write(uint32_t slot, const void *data, size_t size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    uint32_t address;
    static uint8_t program_buffer[PLATFORM_JOURNAL_PROGRAM_UNIT]
        __attribute__((aligned(32)));
    const uint8_t *source = (const uint8_t *)data;
    size_t remaining = size;
    HAL_StatusTypeDef hal_status;

    if (slot >= PLATFORM_JOURNAL_SLOT_COUNT || data == NULL || size == 0U ||
        size > PLATFORM_JOURNAL_SLOT_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    address = slot_address(slot);
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_2;
    erase.Sector = slot == 0U ? FLASH_SECTOR_6 : FLASH_SECTOR_7;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    hal_status = HAL_FLASHEx_Erase(&erase, &sector_error);
    if (hal_status != HAL_OK)
    {
        (void) HAL_FLASH_Lock();
        return FIRMWARE_STATUS_IO_ERROR;
    }

    while (remaining != 0U)
    {
        size_t chunk = remaining < sizeof(program_buffer) ? remaining : sizeof(program_buffer);
        (void) memset(program_buffer, 0xFF, sizeof(program_buffer));
        (void) memcpy(program_buffer, source, chunk);
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                                       (uint32_t)(uintptr_t)program_buffer);
        if (hal_status != HAL_OK)
        {
            (void) HAL_FLASH_Lock();
            return FIRMWARE_STATUS_IO_ERROR;
        }
        address += sizeof(program_buffer);
        source += chunk;
        remaining -= chunk;
    }
    (void) HAL_FLASH_Lock();
    return FIRMWARE_STATUS_OK;
}
