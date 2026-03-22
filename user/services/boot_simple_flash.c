#include "boot_simple_flash.h"

#include <string.h>

#include "boot_extflash.h"
#include "platform/boot_platform.h"
#include "stm32h7xx_hal.h"

#define BOOT_SIMPLE_FLASHWORD_SIZE    32U
#define BOOT_SIMPLE_FLASH_SECTOR_SIZE (128UL * 1024UL)

static const BootSimpleFlashRegion g_boot_info_region = {BOOT_INFO_BASE, BOOT_INFO_SIZE};
static const BootSimpleFlashRegion g_boot_slot_a_region = {BOOT_APP_SLOT_A_BASE, BOOT_APP_SLOT_SIZE};
static const BootSimpleFlashRegion g_boot_slot_b_region = {BOOT_APP_SLOT_B_BASE, BOOT_APP_SLOT_SIZE};

bool Boot_SimpleFlash_IsInternalFlashRange(uint32_t address, uint32_t size) {
    uint32_t end_address;

    if (size == 0U) {
        return false;
    }

    end_address = address + size;
    if (end_address < address) {
        return false;
    }

    return (address >= BOOT_INTERNAL_FLASH_BASE) && (end_address <= BOOT_INTERNAL_FLASH_END);
}

bool Boot_SimpleFlash_IsExtFlashRange(uint32_t address, uint32_t size) {
    return Boot_ExtFlash_IsRangeValid(address, size);
}

bool Boot_SimpleFlash_IsRangeInRegion(const BootSimpleFlashRegion *region, uint32_t address,
                                      uint32_t size) {
    uint32_t end_address;
    uint32_t region_end;

    if ((region == NULL) || (size == 0U)) {
        return false;
    }

    end_address = address + size;
    region_end  = region->base + region->size;
    if ((end_address < address) || (region_end < region->base)) {
        return false;
    }

    return (address >= region->base) && (end_address <= region_end);
}

static bool Boot_SimpleFlash_IsWritableRange(uint32_t address, uint32_t size) {
    return (Boot_SimpleFlash_IsRangeInRegion(&g_boot_info_region, address, size) != false) ||
           (Boot_SimpleFlash_IsRangeInRegion(&g_boot_slot_a_region, address, size) != false) ||
           (Boot_SimpleFlash_IsRangeInRegion(&g_boot_slot_b_region, address, size) != false);
}

static bool Boot_SimpleFlash_AddressToSector(uint32_t address, uint32_t *bank, uint32_t *sector) {
    uint32_t sector_index;

    if ((bank == NULL) || (sector == NULL)) {
        return false;
    }

    if ((address >= FLASH_BANK1_BASE) && (address < FLASH_BANK2_BASE)) {
        sector_index = (address - FLASH_BANK1_BASE) / BOOT_SIMPLE_FLASH_SECTOR_SIZE;
        *bank        = FLASH_BANK_1;
        *sector      = sector_index;
        return (sector_index < FLASH_SECTOR_TOTAL);
    }

#if defined(DUAL_BANK)
    if ((address >= FLASH_BANK2_BASE) && (address <= FLASH_END)) {
        sector_index = (address - FLASH_BANK2_BASE) / BOOT_SIMPLE_FLASH_SECTOR_SIZE;
        *bank        = FLASH_BANK_2;
        *sector      = sector_index;
        return (sector_index < FLASH_SECTOR_TOTAL);
    }
#endif

    return false;
}

static uint32_t Boot_SimpleFlash_GetBankEndExclusive(uint32_t bank) {
#if defined(DUAL_BANK)
    if (bank == FLASH_BANK_1) {
        return FLASH_BANK2_BASE;
    }
#endif
    return BOOT_INTERNAL_FLASH_END;
}

const BootSimpleFlashRegion *Boot_SimpleFlash_GetInfoRegion(void) {
    return &g_boot_info_region;
}

const BootSimpleFlashRegion *Boot_SimpleFlash_GetSlotRegion(uint8_t slot) {
    switch (slot) {
        case SLOT_A:
            return &g_boot_slot_a_region;

        case SLOT_B:
            return &g_boot_slot_b_region;

        default:
            return NULL;
    }
}

BootError Boot_SimpleFlash_Read(uint32_t address, void *buffer, uint32_t size) {
    if ((buffer == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_SimpleFlash_IsInternalFlashRange(address, size) != false) {
        memcpy(buffer, (const void *)(uintptr_t) address, size);
        return BOOT_ERR_NONE;
    }

    if (Boot_SimpleFlash_IsExtFlashRange(address, size) != false) {
        return Boot_ExtFlash_Read(address, buffer, size);
    }

    return BOOT_ERR_FLASH_RANGE;
}

BootError Boot_SimpleFlash_Write(uint32_t address, const void *data, uint32_t size) {
    const uint8_t *source;
    uint32_t write_address;
    uint32_t remaining;
    BootError error;

    if ((data == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_SimpleFlash_IsWritableRange(address, size) == false) {
        return BOOT_ERR_FLASH_RANGE;
    }

    if (Boot_SimpleFlash_IsExtFlashRange(address, size) != false) {
        return Boot_ExtFlash_Write(address, data, size);
    }

    if (Boot_SimpleFlash_IsInternalFlashRange(address, size) == false) {
        return BOOT_ERR_FLASH_RANGE;
    }

    error = Boot_Platform_FlashUnlock();
    if (error != BOOT_ERR_NONE) {
        return BOOT_ERR_FLASH_WRITE;
    }

    Boot_Platform_FlashClearAllFlags();

    source        = (const uint8_t *) data;
    write_address = address;
    remaining     = size;
    while (remaining > 0U) {
        uint32_t aligned_address = write_address & ~(BOOT_SIMPLE_FLASHWORD_SIZE - 1U);
        uint32_t word_offset     = write_address - aligned_address;
        uint32_t copy_size       = BOOT_SIMPLE_FLASHWORD_SIZE - word_offset;
        uint8_t flash_word[BOOT_SIMPLE_FLASHWORD_SIZE];

        if (copy_size > remaining) {
            copy_size = remaining;
        }

        memcpy(flash_word, (const void *)(uintptr_t) aligned_address, sizeof(flash_word));
        memcpy(&flash_word[word_offset], source, copy_size);

        error = Boot_Platform_FlashProgramFlashWord(aligned_address, flash_word);
        if (error != BOOT_ERR_NONE) {
            Boot_Platform_FlashLock();
            return BOOT_ERR_FLASH_WRITE;
        }

        if (memcmp((const void *)(uintptr_t) aligned_address, flash_word, sizeof(flash_word)) != 0) {
            Boot_Platform_FlashLock();
            return BOOT_ERR_FLASH_VERIFY;
        }

        write_address += copy_size;
        source += copy_size;
        remaining -= copy_size;
    }

    Boot_Platform_FlashRefreshCache();
    Boot_Platform_FlashLock();
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleFlash_EraseRegion(uint32_t address, uint32_t size) {
    uint32_t current_address;
    uint32_t end_address;
    BootError error;

    if ((size == 0U) || (Boot_SimpleFlash_IsWritableRange(address, size) == false)) {
        return BOOT_ERR_FLASH_RANGE;
    }

    if (Boot_SimpleFlash_IsExtFlashRange(address, size) != false) {
        return Boot_ExtFlash_Erase(address, size);
    }

    end_address = address + size - 1U;
    if ((end_address < address) || (Boot_SimpleFlash_IsInternalFlashRange(address, size) == false)) {
        return BOOT_ERR_FLASH_RANGE;
    }

    error = Boot_Platform_FlashUnlock();
    if (error != BOOT_ERR_NONE) {
        return BOOT_ERR_FLASH_ERASE;
    }

    Boot_Platform_FlashClearAllFlags();

    current_address = address;
    while (current_address <= end_address) {
        uint32_t bank;
        uint32_t start_sector;
        uint32_t segment_end;
        uint32_t end_sector;

        if (Boot_SimpleFlash_AddressToSector(current_address, &bank, &start_sector) == false) {
            Boot_Platform_FlashLock();
            return BOOT_ERR_FLASH_RANGE;
        }

        segment_end = Boot_SimpleFlash_GetBankEndExclusive(bank) - 1U;
        if (segment_end > end_address) {
            segment_end = end_address;
        }

        if (Boot_SimpleFlash_AddressToSector(segment_end, &bank, &end_sector) == false) {
            Boot_Platform_FlashLock();
            return BOOT_ERR_FLASH_RANGE;
        }

        error =
            Boot_Platform_FlashEraseSectors(bank, start_sector, (end_sector - start_sector) + 1U);
        if (error != BOOT_ERR_NONE) {
            Boot_Platform_FlashLock();
            return error;
        }

        current_address = segment_end + 1U;
    }

    Boot_Platform_FlashRefreshCache();
    Boot_Platform_FlashLock();
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleFlash_EraseInfoRegion(void) {
    return Boot_SimpleFlash_EraseRegion(g_boot_info_region.base, g_boot_info_region.size);
}

BootError Boot_SimpleFlash_EraseSlot(uint8_t slot) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetSlotRegion(slot);

    if (region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    return Boot_SimpleFlash_EraseRegion(region->base, region->size);
}
