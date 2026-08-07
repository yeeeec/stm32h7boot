/**
 * @file relocation_types.h
 * @brief Stable HMI_RELOC_V1 table domain types.
 */
#ifndef SERVICES_RELOCATION_TYPES_H
#define SERVICES_RELOCATION_TYPES_H

#include <stdint.h>

#define HMI_RELOCATION_ENTRY_SIZE                 8U
#define HMI_RELOCATION_ABS32_ADD_XIP_BASE          1U

/** One decoded HMI_RELOC_V1 entry. */
typedef struct
{
    uint32_t target_offset;
    uint16_t type;
    uint16_t reserved;
} hmi_relocation_entry_t;

#endif
