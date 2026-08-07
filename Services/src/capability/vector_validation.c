/**
 * @file vector_validation.c
 * @brief Cortex-M7 vector-table validation implementation.
 */
#include "services/capability/vector_validation.h"

#include <stddef.h>

#include "services/capability/checked_arithmetic.h"
#include "services/capability/slot_policy.h"

static int StackPointerInRegion(uint32_t address, const memory_region_t *region)
{
    uint32_t end;

    if (!CheckedArithmetic_RangeEndU32(region->start_address, region->size, &end))
    {
        return 0;
    }

    /* A descending stack may start one byte past the addressable SRAM region. */
    return (address > region->start_address) && (address <= end);
}

firmware_status_t VectorValidation_Validate(const vector_table_values_t *vectors,
                                            const boot_region_t *app_region,
                                            uint32_t app_image_size,
                                            const memory_region_t *sram_regions,
                                            uint32_t sram_region_count)
{
    uint32_t index;
    uint32_t reset_address;
    int msp_valid = 0;

    if ((vectors == NULL) || (app_region == NULL) || (sram_regions == NULL) ||
        (sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (!FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(app_region, app_image_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    for (index = 0U; index < sram_region_count; ++index)
    {
        if (StackPointerInRegion(vectors->initial_msp, &sram_regions[index]))
        {
            msp_valid = 1;
            break;
        }
    }
    if (((vectors->initial_msp & 0x7U) != 0U) || (msp_valid == 0))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if ((vectors->reset_handler & 0x1U) == 0U)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    reset_address = vectors->reset_handler & ~0x1UL;
    if ((reset_address < app_region->mapped_address) ||
        ((reset_address - app_region->mapped_address) >= app_image_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    return FIRMWARE_STATUS_OK;
}
