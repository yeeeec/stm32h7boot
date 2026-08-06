/**
 * @file boot_types.h
 * @brief Stable Bootloader domain types shared by service APIs.
 */
#ifndef SERVICES_BOOT_TYPES_H
#define SERVICES_BOOT_TYPES_H

#include <stdint.h>

typedef enum
{
    BOOT_PAIR_NONE = 0,
    BOOT_PAIR_1 = 1,
    BOOT_PAIR_2 = 2
} boot_pair_t;

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

typedef struct
{
    boot_pair_t pair;
    boot_region_t app;
    boot_region_t gui;
} boot_pair_layout_t;

#endif
