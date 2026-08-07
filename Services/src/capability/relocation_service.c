/**
 * @file relocation_service.c
 * @brief Streaming ABS32_ADD_XIP_BASE relocation implementation.
 */
#include "services/capability/relocation_service.h"

#include <stddef.h>

#include "services/capability/checked_arithmetic.h"

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8U);
    data[2] = (uint8_t) (value >> 16U);
    data[3] = (uint8_t) (value >> 24U);
}

firmware_status_t RelocationService_Init(relocation_service_t *service, uint32_t image_size,
                                         uint32_t target_xip_base)
{
    return RelocationService_InitEx(service, image_size, 0U, target_xip_base);
}

firmware_status_t RelocationService_InitEx(relocation_service_t *service, uint32_t image_size,
                                           uint32_t source_xip_base, uint32_t target_xip_base)
{
    if ((service == NULL) || (image_size == 0U) || (target_xip_base == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    service->image_size             = image_size;
    service->source_xip_base        = source_xip_base;
    service->target_xip_base        = target_xip_base;
    service->next_block_offset      = 0U;
    service->last_relocation_offset = 0U;
    service->processed_relocations  = 0U;
    service->has_relocation         = 0;
    service->initialized            = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RelocationService_DecodeEntry(const uint8_t bytes[HMI_RELOCATION_ENTRY_SIZE],
                                                hmi_relocation_entry_t *entry)
{
    if ((bytes == NULL) || (entry == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    entry->target_offset = ReadU32(&bytes[0]);
    entry->type          = ReadU16(&bytes[4]);
    entry->reserved      = ReadU16(&bytes[6]);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RelocationService_ApplyBlock(relocation_service_t *service, uint32_t block_offset,
                                               uint8_t *data, uint32_t data_size,
                                               const hmi_relocation_entry_t *entries,
                                               uint32_t entry_count)
{
    uint32_t block_end;
    uint32_t index;
    uint32_t validation_last_offset;
    int validation_has_relocation;

    if ((service == NULL) || (data == NULL) || (data_size == 0U) ||
        ((entries == NULL) && (entry_count != 0U)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (block_offset != service->next_block_offset))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!CheckedArithmetic_AddU32(block_offset, data_size, &block_end) ||
        (block_end > service->image_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    validation_last_offset    = service->last_relocation_offset;
    validation_has_relocation = service->has_relocation;
    for (index = 0U; index < entry_count; ++index)
    {
        const hmi_relocation_entry_t *entry = &entries[index];
        uint32_t entry_end;
        uint32_t local_offset;
        uint32_t raw_word;
        uint32_t canonical_word;
        uint32_t ignored_relocated_word;

        if ((entry->type != HMI_RELOCATION_ABS32_ADD_XIP_BASE) || (entry->reserved != 0U) ||
            ((entry->target_offset & 3U) != 0U) || (entry->target_offset == 0U) ||
            ((validation_has_relocation != 0) && (entry->target_offset <= validation_last_offset)))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if (!CheckedArithmetic_AddU32(entry->target_offset, sizeof(uint32_t), &entry_end) ||
            (entry->target_offset < block_offset) || (entry_end > block_end))
        {
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        }

        local_offset = entry->target_offset - block_offset;
        raw_word     = ReadU32(&data[local_offset]);
        if (raw_word < service->source_xip_base)
        {
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        }
        canonical_word = raw_word - service->source_xip_base;
        if (!CheckedArithmetic_AddU32(canonical_word, service->target_xip_base,
                                      &ignored_relocated_word))
        {
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        }
        validation_last_offset    = entry->target_offset;
        validation_has_relocation = 1;
    }

    for (index = 0U; index < entry_count; ++index)
    {
        const hmi_relocation_entry_t *entry = &entries[index];
        uint32_t local_offset               = entry->target_offset - block_offset;
        uint32_t raw_word                   = ReadU32(&data[local_offset]);
        uint32_t relocated_word = (raw_word - service->source_xip_base) + service->target_xip_base;

        WriteU32(&data[local_offset], relocated_word);
    }

    if (entry_count != 0U)
    {
        service->last_relocation_offset = entries[entry_count - 1U].target_offset;
        service->has_relocation         = 1;
        service->processed_relocations += entry_count;
    }
    service->next_block_offset = block_end;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RelocationService_Finish(const relocation_service_t *service,
                                           uint32_t expected_relocations)
{
    if (service == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->next_block_offset != service->image_size) ||
        (service->processed_relocations != expected_relocations))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
