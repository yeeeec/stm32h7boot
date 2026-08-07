/**
 * @file slot_policy.c
 * @brief Fixed APP/GUI partition policy implementation.
 */
#include "services/capability/slot_policy.h"

#include <stddef.h>

#include "services/capability/checked_arithmetic.h"

#define QSPI_MAPPED_BASE 0x90000000UL
#define APP_CAPACITY     (1UL * 1024UL * 1024UL)
#define GUI_CAPACITY     (8UL * 1024UL * 1024UL)

static const boot_pair_layout_t pair_layouts[] = {
    {
        .pair = BOOT_PAIR_1,
        .app = {0x00000000UL, QSPI_MAPPED_BASE + 0x00000000UL, APP_CAPACITY},
        .gui = {0x00200000UL, QSPI_MAPPED_BASE + 0x00200000UL, GUI_CAPACITY},
    },
    {
        .pair = BOOT_PAIR_2,
        /* Keep the APP and GUI relative layout identical to pair-1 while
         * leaving a complete non-overlapping copy for atomic A/B updates. */
        .app = {0x00A00000UL, QSPI_MAPPED_BASE + 0x00A00000UL, APP_CAPACITY},
        .gui = {0x00C00000UL, QSPI_MAPPED_BASE + 0x00C00000UL, GUI_CAPACITY},
    },
};

firmware_status_t SlotPolicy_GetPairLayout(
    boot_pair_t pair,
    boot_pair_layout_t *layout)
{
    if (layout == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((pair != BOOT_PAIR_1) && (pair != BOOT_PAIR_2))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    *layout = pair_layouts[(uint32_t)pair - 1U];
    return FIRMWARE_STATUS_OK;
}

firmware_status_t SlotPolicy_SelectInactivePair(
    boot_pair_t active_pair,
    boot_pair_t *inactive_pair)
{
    if (inactive_pair == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (active_pair == BOOT_PAIR_1)
    {
        *inactive_pair = BOOT_PAIR_2;
        return FIRMWARE_STATUS_OK;
    }
    if (active_pair == BOOT_PAIR_2)
    {
        *inactive_pair = BOOT_PAIR_1;
        return FIRMWARE_STATUS_OK;
    }

    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t SlotPolicy_ValidateImageSize(
    const boot_region_t *region,
    uint32_t image_size)
{
    if (region == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((image_size == 0U) || (image_size > region->capacity_bytes))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    return FIRMWARE_STATUS_OK;
}

int SlotPolicy_ContainsRange(
    const boot_region_t *region,
    uint32_t flash_offset,
    uint32_t size)
{
    uint32_t region_end;
    uint32_t range_end;

    if ((region == NULL) ||
        !CheckedArithmetic_RangeEndU32(
            region->flash_offset, region->capacity_bytes, &region_end) ||
        !CheckedArithmetic_RangeEndU32(flash_offset, size, &range_end))
    {
        return 0;
    }

    return (flash_offset >= region->flash_offset) && (range_end <= region_end);
}

firmware_status_t SlotPolicy_ValidateStorageGeometry(
    uint32_t capacity_bytes,
    uint32_t erase_size)
{
    if ((capacity_bytes != SLOT_POLICY_FLASH_CAPACITY_BYTES) ||
        (erase_size != SLOT_POLICY_ERASE_SIZE_BYTES))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    return FIRMWARE_STATUS_OK;
}
