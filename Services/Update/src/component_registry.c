#include "update/component_registry.h"

#include <string.h>

#include "platform/platform_memory_map.h"
#include "platform/platform_flash.h"

// clang-format off
static const update_component_descriptor_t s_components[] = {
    {"app",     1U, IMAGE_TARGET_APP,       "raw-bin-v1",       PLATFORM_APP_MAX_SIZE,      
        PLATFORM_APP_OFFSET,                PLATFORM_FLASH_ERASE_SIZE, 1U, 4U},
    {"gui",     2U, IMAGE_TARGET_GUI,       "raw-bin-v1",       PLATFORM_GUI_MAX_SIZE,      
        PLATFORM_GUI_OFFSET,                PLATFORM_FLASH_ERASE_SIZE, 1U, 2U},
    {"therapy", 4U, IMAGE_TARGET_THERAPY,   "raw-bin-v1",       PLATFORM_THERAPY_MAX_SIZE,  
        PLATFORM_THERAPY_TARGET_ADDRESS,    PLATFORM_FLASH_ERASE_SIZE, 1U, 3U},
    {"voice",   8U, IMAGE_TARGET_VOICE,     "voice-bin-v1",     PLATFORM_VOICE_MAX_SIZE,    
        PLATFORM_VOICE_OFFSET,              PLATFORM_FLASH_ERASE_SIZE, 1U, 0U},
    {"config",  16U, IMAGE_TARGET_CONFIG,   "config-bin-v1",    PLATFORM_CONFIG_MAX_SIZE,   
        PLATFORM_CONFIG_OFFSET,             PLATFORM_FLASH_ERASE_SIZE, 1U, 1U},
};
// clang-format on

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

uint32_t UpdateComponent_RequiredMask(void)
{
    uint32_t mask = 0U;
    size_t index;

    for (index = 0U; index < sizeof(s_components) / sizeof(s_components[0]); ++index)
        mask |= s_components[index].mask_bit;
    return mask;
}
