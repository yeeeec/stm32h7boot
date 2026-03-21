#include "boot_simple_flash.h"

#include <string.h>

#include "boot_config.h"
#include "boot_platform.h"

#define BOOT_SIMPLE_FLASHWORD_SIZE    32U
#define BOOT_SIMPLE_FLASH_SECTOR_SIZE (128UL * 1024UL)

static const BootSimpleFlashRegion g_boot_app_region = {BOOT_APP_BASE, BOOT_APP_SIZE};

static bool Boot_SimpleFlash_IsInternalFlashRange(uint32_t address, uint32_t size) {
    uint32_t end_address;
    uint32_t flash_end;

    if (size == 0U) {
        return false;
    }

    end_address = address + size;
    flash_end   = BOOT_INTERNAL_FLASH_BASE + BOOT_INTERNAL_FLASH_SIZE;
    if (end_address < address) {
        return false;
    }

    return (address >= BOOT_INTERNAL_FLASH_BASE) && (end_address <= flash_end);
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

const BootSimpleFlashRegion *Boot_SimpleFlash_GetAppRegion(void) {
    return &g_boot_app_region;
}

bool Boot_SimpleFlash_IsAppRange(uint32_t address, uint32_t size) {
    uint32_t end_address;
    uint32_t app_end;

    if (size == 0U) {
        return false;
    }

    end_address = address + size;
    app_end     = g_boot_app_region.base + g_boot_app_region.size;
    if (end_address < address) {
        return false;
    }

    return (address >= g_boot_app_region.base) && (end_address <= app_end);
}

BootError Boot_SimpleFlash_Read(uint32_t address, void *buffer, uint32_t size) {
    if ((buffer == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_SimpleFlash_IsInternalFlashRange(address, size) == false) {
        return BOOT_ERR_FLASH_RANGE;
    }

    memcpy(buffer, (const void *) address, size);
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleFlash_Write(uint32_t address, const void *data, uint32_t size) {
    const uint8_t *source;
    uint32_t write_address;
    uint32_t remaining;
    BootError error;

    if ((data == NULL) || (size == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if ((Boot_SimpleFlash_IsInternalFlashRange(address, size) == false) ||
        (Boot_SimpleFlash_IsAppRange(address, size) == false)) {
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

        memcpy(flash_word, (const void *) aligned_address, sizeof(flash_word));
        memcpy(&flash_word[word_offset], source, copy_size);

        error = Boot_Platform_FlashProgramFlashWord(aligned_address, flash_word);
        if (error != BOOT_ERR_NONE) {
            Boot_Platform_FlashLock();
            return BOOT_ERR_FLASH_WRITE;
        }

        if (memcmp((const void *) aligned_address, flash_word, sizeof(flash_word)) != 0) {
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

BootError Boot_SimpleFlash_EraseApp(void) {
    uint32_t start_bank;
    uint32_t end_bank;
    uint32_t start_sector;
    uint32_t end_sector;
    uint32_t end_address;
    BootError error;

    end_address = g_boot_app_region.base + g_boot_app_region.size - 1U;
    if ((Boot_SimpleFlash_AddressToSector(g_boot_app_region.base, &start_bank, &start_sector) ==
         false) ||
        (Boot_SimpleFlash_AddressToSector(end_address, &end_bank, &end_sector) == false)) {
        return BOOT_ERR_FLASH_RANGE;
    }

    error = Boot_Platform_FlashUnlock();
    if (error != BOOT_ERR_NONE) {
        return BOOT_ERR_FLASH_ERASE;
    }

    Boot_Platform_FlashClearAllFlags();

    if (start_bank != end_bank) {
        Boot_Platform_FlashLock();
        return BOOT_ERR_FLASH_RANGE;
    }

    error =
        Boot_Platform_FlashEraseSectors(start_bank, start_sector, (end_sector - start_sector) + 1U);
    if (error == BOOT_ERR_NONE) {
        Boot_Platform_FlashRefreshCache();
    }

    Boot_Platform_FlashLock();
    return error;
}
