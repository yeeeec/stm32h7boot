/**
 * @file appx_types.h
 * @brief Stable HMI XIP APP container and relocation domain types.
 */
#ifndef SERVICES_APPX_TYPES_H
#define SERVICES_APPX_TYPES_H

#include <stdint.h>

#define APPX_HEADER_SIZE             64U
#define APPX_RELOCATION_ENTRY_SIZE   8U
#define APPX_RELOCATION_ABS32_ADD_XIP_BASE 1U

/** Parsed HMI_XIP_APP_V1 container geometry and integrity fields. */
typedef struct
{
    uint32_t image_size;
    uint32_t vector_offset;
    uint32_t entry_offset;
    uint32_t relocation_offset;
    uint32_t relocation_count;
    uint32_t image_crc32;
    uint32_t relocation_crc32;
} appx_header_t;

/** One decoded HMI_RELOC_V1 entry. */
typedef struct
{
    uint32_t target_offset;
    uint16_t type;
    uint16_t reserved;
} appx_relocation_entry_t;

#endif
