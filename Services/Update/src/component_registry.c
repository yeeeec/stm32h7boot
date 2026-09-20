#include "update_internal.h"

#include <string.h>

#include "bootloader_config.h"
#include "platform/platform_memory_map.h"
#include "platform/platform_flash.h"

static const update_component_descriptor_t s_components[] = {
    {"app", 1U, IMAGE_TARGET_APP, "raw-bin-v1", PLATFORM_APP_MAX_SIZE,
     PLATFORM_APP_OFFSET, PLATFORM_FLASH_ERASE_SIZE, 1U, 4U},
    {"gui", 2U, IMAGE_TARGET_GUI, "raw-bin-v1", PLATFORM_GUI_MAX_SIZE,
     PLATFORM_GUI_OFFSET, PLATFORM_FLASH_ERASE_SIZE, 1U, 2U},
    {"therapy", 4U, IMAGE_TARGET_THERAPY, "raw-bin-v1", PLATFORM_THERAPY_MAX_SIZE,
     PLATFORM_THERAPY_TARGET_ADDRESS, PLATFORM_FLASH_ERASE_SIZE, 1U, 3U},
    {"voice", 8U, IMAGE_TARGET_VOICE, "voice-bin-v1", PLATFORM_VOICE_MAX_SIZE,
     PLATFORM_VOICE_OFFSET, PLATFORM_FLASH_ERASE_SIZE, 1U, 0U},
    {"config", 16U, IMAGE_TARGET_CONFIG, "config-bin-v1", PLATFORM_CONFIG_MAX_SIZE,
     PLATFORM_CONFIG_OFFSET, PLATFORM_FLASH_ERASE_SIZE, 1U, 1U},
};

const update_component_descriptor_t *UpdateComponent_Find(const char *name)
{
    size_t index;
    if (name == NULL)
        return NULL;
    for (index = 0U; index < sizeof(s_components) / sizeof(s_components[0]); ++index)
        if (strcmp(name, s_components[index].name) == 0)
            return &s_components[index];
    return NULL;
}

const update_component_descriptor_t *UpdateComponent_FindByTarget(image_target_t target)
{
    size_t index;
    for (index = 0U; index < sizeof(s_components) / sizeof(s_components[0]); ++index)
        if (target == s_components[index].target)
            return &s_components[index];
    return NULL;
}

int UpdateComponent_IsEnabled(const update_component_descriptor_t *descriptor)
{
    if (descriptor == NULL)
        return 0;
#if (BOOTLOADER_UPDATE_APP_GUI_ONLY == 1U)
    return descriptor->target == IMAGE_TARGET_APP || descriptor->target == IMAGE_TARGET_GUI;
#else
    return 1;
#endif
}

uint32_t UpdateComponent_DeriveMask(const update_manifest_t *manifest)
{
    uint32_t mask = 0U;
    size_t index;
    if (manifest == NULL)
        return 0U;
    for (index = 0U; index < manifest->component_count; ++index)
        mask |= manifest->components[index].mask_bit;
    return mask;
}

firmware_status_t UpdateComponent_ValidateRanges(const update_manifest_t *manifest)
{
    size_t left;
    if (manifest == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (left = 0U; left < manifest->component_count; ++left)
    {
        const update_component_descriptor_t *a =
            UpdateComponent_Find(manifest->components[left].name);
        uint64_t a_end;
        size_t right;
        if (a == NULL)
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        if (!UpdateComponent_IsEnabled(a))
            continue;
        if (manifest->components[left].size == 0U ||
            manifest->components[left].size > a->maximum_size)
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        if ((a->address % a->erase_size) != 0U ||
            (((uint64_t) manifest->components[left].size + a->erase_size - 1U) /
             a->erase_size) * a->erase_size > a->maximum_size)
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        a_end = (uint64_t) a->address + manifest->components[left].size;
        if (a_end > (uint64_t) a->address + a->maximum_size)
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        if ((manifest->components[left].size % a->write_alignment) != 0U &&
            a->target != IMAGE_TARGET_APP && a->target != IMAGE_TARGET_GUI &&
            a->target != IMAGE_TARGET_VOICE && a->target != IMAGE_TARGET_CONFIG)
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        for (right = left + 1U; right < manifest->component_count; ++right)
        {
            const update_component_descriptor_t *b =
                UpdateComponent_Find(manifest->components[right].name);
            uint64_t b_end;
            if (b == NULL)
                return FIRMWARE_STATUS_NOT_SUPPORTED;
            if (!UpdateComponent_IsEnabled(b))
                continue;
            b_end = (uint64_t) b->address + manifest->components[right].size;
            if (a->target != IMAGE_TARGET_THERAPY && b->target != IMAGE_TARGET_THERAPY &&
                (uint64_t) a->address < b_end && (uint64_t) b->address < a_end)
                return FIRMWARE_STATUS_OUT_OF_RANGE;
        }
    }
    return FIRMWARE_STATUS_OK;
}
