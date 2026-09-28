#ifndef FIRMWARE_UPDATE_COMPONENT_REGISTRY_H
#define FIRMWARE_UPDATE_COMPONENT_REGISTRY_H

#include <stdint.h>

#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *name;
        uint32_t mask_bit;
        image_target_t target;
        const char *format;
        uint32_t maximum_size;
        uint32_t address;
        uint32_t erase_size;
        uint32_t write_alignment;
        uint8_t installation_order;
    } update_component_descriptor_t;

    const update_component_descriptor_t *UpdateComponent_Find(const char *name);
    const update_component_descriptor_t *UpdateComponent_FindByTarget(image_target_t target);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_UPDATE_COMPONENT_REGISTRY_H */
