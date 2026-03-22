#include "boot_simple_flash.h"

#include <stddef.h>
#include <string.h>

#include "boot_crc32.h"
#include "platform/boot_platform.h"

#define BOOT_SIMPLE_FLASHWORD_SIZE    32U
#define BOOT_SIMPLE_FLASH_SECTOR_SIZE (128UL * 1024UL)

typedef struct
{
    BootSimpleFlashLayout layout;
    uint8_t padding[BOOT_CONFIG_RECORD_SIZE - sizeof(BootSimpleFlashLayout)];
} BootSimpleFlashLayoutRecord;

_Static_assert(sizeof(BootSimpleFlashLayout) <= BOOT_CONFIG_RECORD_SIZE,
               "BootSimpleFlashLayout must fit in BOOT_CONFIG_RECORD_SIZE");

static const BootSimpleFlashRegion g_boot_boot_region = {BOOT_BOOT_BASE, BOOT_BOOT_SIZE};
static const BootSimpleFlashRegion g_boot_info_region = {BOOT_INFO_BASE, BOOT_INFO_SIZE};
static BootSimpleFlashLayout g_boot_layout;
static uint8_t g_boot_layout_loaded;

static uint32_t Boot_SimpleFlash_CalcLayoutCrc(const BootSimpleFlashLayout *layout) {
    if (layout == NULL) {
        return 0U;
    }

    return Boot_Crc32_IsoCalc(layout, offsetof(BootSimpleFlashLayout, crc));
}

static bool Boot_SimpleFlash_IsRegionBlank(uint32_t address, uint32_t size) {
    const uint8_t *data = (const uint8_t *) (uintptr_t) address;
    uint32_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static bool Boot_SimpleFlash_IsRegionSectorAligned(const BootSimpleFlashRegion *region) {
    if (region == NULL) {
        return false;
    }

    return ((region->base % BOOT_SIMPLE_FLASH_SECTOR_SIZE) == 0U) &&
           ((region->size % BOOT_SIMPLE_FLASH_SECTOR_SIZE) == 0U);
}

static bool Boot_SimpleFlash_DoRegionsOverlap(const BootSimpleFlashRegion *lhs,
                                              const BootSimpleFlashRegion *rhs) {
    uint32_t lhs_end;
    uint32_t rhs_end;

    if ((lhs == NULL) || (rhs == NULL) || (lhs->size == 0U) || (rhs->size == 0U)) {
        return false;
    }

    lhs_end = lhs->base + lhs->size;
    rhs_end = rhs->base + rhs->size;
    if ((lhs_end < lhs->base) || (rhs_end < rhs->base)) {
        return true;
    }

    return (lhs->base < rhs_end) && (rhs->base < lhs_end);
}

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

static bool Boot_SimpleFlash_IsSlotRegionValid(const BootSimpleFlashRegion *region) {
    if ((region == NULL) || (Boot_SimpleFlash_IsInternalFlashRange(region->base, region->size) == false)) {
        return false;
    }

    if (Boot_SimpleFlash_IsRegionSectorAligned(region) == false) {
        return false;
    }

    if (Boot_SimpleFlash_DoRegionsOverlap(region, &g_boot_boot_region) != false) {
        return false;
    }

    if (Boot_SimpleFlash_DoRegionsOverlap(region, &g_boot_info_region) != false) {
        return false;
    }

    return true;
}

static void Boot_SimpleFlash_NormalizeLayout(BootSimpleFlashLayout *layout) {
    if (layout == NULL) {
        return;
    }

    layout->magic      = BOOT_CONFIG_MAGIC;
    layout->version    = BOOT_CONFIG_STRUCT_VERSION;
    layout->slot_count = BOOT_APP_SLOT_COUNT;
    layout->crc        = Boot_SimpleFlash_CalcLayoutCrc(layout);
}

static bool Boot_SimpleFlash_IsLayoutValid(const BootSimpleFlashLayout *layout) {
    uint32_t slot_index;

    if (layout == NULL) {
        return false;
    }

    if ((layout->magic != BOOT_CONFIG_MAGIC) || (layout->version != BOOT_CONFIG_STRUCT_VERSION) ||
        (layout->slot_count != BOOT_APP_SLOT_COUNT)) {
        return false;
    }

    if (layout->crc != Boot_SimpleFlash_CalcLayoutCrc(layout)) {
        return false;
    }

    for (slot_index = 0U; slot_index < BOOT_APP_SLOT_COUNT; ++slot_index) {
        if (Boot_SimpleFlash_IsSlotRegionValid(&layout->slot_regions[slot_index]) == false) {
            return false;
        }
    }

    return Boot_SimpleFlash_DoRegionsOverlap(&layout->slot_regions[SLOT_A],
                                             &layout->slot_regions[SLOT_B]) == false;
}

static void Boot_SimpleFlash_SetCachedLayout(const BootSimpleFlashLayout *layout) {
    if (layout == NULL) {
        return;
    }

    g_boot_layout        = *layout;
    g_boot_layout_loaded = 1U;
}

static void Boot_SimpleFlash_EnsureLayoutLoaded(void) {
    BootSimpleFlashLayout layout;

    if (g_boot_layout_loaded != 0U) {
        return;
    }

    if (Boot_SimpleFlash_LoadLayout(&layout) == BOOT_ERR_NONE) {
        Boot_SimpleFlash_SetCachedLayout(&layout);
        return;
    }

    Boot_SimpleFlash_InitDefaultLayout(&layout);
    Boot_SimpleFlash_SetCachedLayout(&layout);
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
    Boot_SimpleFlash_EnsureLayoutLoaded();
    return (Boot_SimpleFlash_IsRangeInRegion(&g_boot_info_region, address, size) != false) ||
           (Boot_SimpleFlash_IsRangeInRegion(&g_boot_layout.slot_regions[SLOT_A], address, size) !=
            false) ||
           (Boot_SimpleFlash_IsRangeInRegion(&g_boot_layout.slot_regions[SLOT_B], address, size) !=
            false);
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

void Boot_SimpleFlash_InitDefaultLayout(BootSimpleFlashLayout *layout) {
    if (layout == NULL) {
        return;
    }

    memset(layout, 0, sizeof(*layout));
    layout->slot_regions[SLOT_A].base = BOOT_DEFAULT_APP1_BASE;
    layout->slot_regions[SLOT_A].size = BOOT_DEFAULT_APP1_SIZE;
    layout->slot_regions[SLOT_B].base = BOOT_DEFAULT_APP2_BASE;
    layout->slot_regions[SLOT_B].size = BOOT_DEFAULT_APP2_SIZE;
    Boot_SimpleFlash_NormalizeLayout(layout);
}

BootError Boot_SimpleFlash_LoadLayout(BootSimpleFlashLayout *layout) {
    const BootSimpleFlashLayoutRecord *record;

    if (layout == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_SimpleFlash_IsRegionBlank(BOOT_CONFIG_BASE, BOOT_CONFIG_RECORD_SIZE) != false) {
        return BOOT_ERR_CTRL_NOT_FOUND;
    }

    record = (const BootSimpleFlashLayoutRecord *) (uintptr_t) BOOT_CONFIG_BASE;
    if (Boot_SimpleFlash_IsLayoutValid(&record->layout) == false) {
        return BOOT_ERR_CTRL_CRC;
    }

    *layout = record->layout;
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleFlash_WriteLayout(const BootSimpleFlashLayout *layout) {
    BootSimpleFlashLayout normalized;
    BootSimpleFlashLayoutRecord record;
    BootError error;

    if (layout == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    normalized = *layout;
    Boot_SimpleFlash_NormalizeLayout(&normalized);
    if (Boot_SimpleFlash_IsLayoutValid(&normalized) == false) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    memset(&record, 0xFF, sizeof(record));
    record.layout = normalized;

    error = Boot_SimpleFlash_Write(BOOT_CONFIG_BASE, &record, sizeof(record));
    if (error == BOOT_ERR_NONE) {
        Boot_SimpleFlash_SetCachedLayout(&normalized);
    }

    return error;
}

BootError Boot_SimpleFlash_StoreLayout(const BootSimpleFlashLayout *layout) {
    BootSimpleFlashLayout normalized;
    s_BootInfo boot_info;
    BootError info_error;
    BootError error;

    if (layout == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    normalized = *layout;
    Boot_SimpleFlash_NormalizeLayout(&normalized);
    if (Boot_SimpleFlash_IsLayoutValid(&normalized) == false) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    info_error = Boot_Info_Load(&boot_info);
    if ((info_error != BOOT_ERR_NONE) && (info_error != BOOT_ERR_CTRL_NOT_FOUND) &&
        (info_error != BOOT_ERR_CTRL_CRC)) {
        return info_error;
    }

    error = Boot_SimpleFlash_EraseInfoRegion();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (info_error == BOOT_ERR_NONE) {
        error = Boot_Info_Store(&boot_info);
        if (error != BOOT_ERR_NONE) {
            return error;
        }
    }

    return Boot_SimpleFlash_WriteLayout(&normalized);
}

BootError Boot_SimpleFlash_EnsureLayoutPersisted(void) {
    BootSimpleFlashLayout layout;
    BootError error;

    error = Boot_SimpleFlash_LoadLayout(&layout);
    if (error == BOOT_ERR_NONE) {
        Boot_SimpleFlash_SetCachedLayout(&layout);
        return BOOT_ERR_NONE;
    }

    Boot_SimpleFlash_InitDefaultLayout(&layout);
    Boot_SimpleFlash_SetCachedLayout(&layout);

    if (Boot_SimpleFlash_IsRegionBlank(BOOT_CONFIG_BASE, BOOT_CONFIG_RECORD_SIZE) != false) {
        return Boot_SimpleFlash_WriteLayout(&layout);
    }

    return Boot_SimpleFlash_StoreLayout(&layout);
}

const BootSimpleFlashRegion *Boot_SimpleFlash_GetInfoRegion(void) {
    return &g_boot_info_region;
}

const BootSimpleFlashRegion *Boot_SimpleFlash_GetSlotRegion(uint8_t slot) {
    Boot_SimpleFlash_EnsureLayoutLoaded();

    switch (slot) {
        case SLOT_A:
            return &g_boot_layout.slot_regions[SLOT_A];

        case SLOT_B:
            return &g_boot_layout.slot_regions[SLOT_B];

        default:
            return NULL;
    }
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
        (Boot_SimpleFlash_IsWritableRange(address, size) == false)) {
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

BootError Boot_SimpleFlash_EraseRegion(uint32_t address, uint32_t size) {
    uint32_t current_address;
    uint32_t end_address;
    BootError error;

    if ((size == 0U) || (Boot_SimpleFlash_IsWritableRange(address, size) == false)) {
        return BOOT_ERR_FLASH_RANGE;
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
