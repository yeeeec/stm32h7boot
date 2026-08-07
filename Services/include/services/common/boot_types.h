/**
 * @file boot_types.h
 * @brief Stable Bootloader domain types shared by service APIs.
 */
#ifndef SERVICES_BOOT_TYPES_H
#define SERVICES_BOOT_TYPES_H

#include <stdint.h>

typedef enum
{
    BOOT_COMPONENT_APP = 0,
    BOOT_COMPONENT_GUI = 1
} boot_component_t;

typedef struct
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} release_version_t;

typedef struct
{
    uint32_t flash_offset;
    uint32_t mapped_address;
    uint32_t capacity_bytes;
} boot_region_t;

#endif
