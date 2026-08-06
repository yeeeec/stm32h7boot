/**
 * @file vector_validation.h
 * @brief Cortex-M7 initial MSP and Reset Handler validation policy.
 */
#ifndef SERVICES_VECTOR_VALIDATION_H
#define SERVICES_VECTOR_VALIDATION_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/boot_types.h"

typedef struct
{
    uint32_t start_address;
    uint32_t size;
} memory_region_t;

typedef struct
{
    uint32_t initial_msp;
    uint32_t reset_handler;
} vector_table_values_t;

/**
 * Validate vector values against the selected APP image and allowed SRAM regions.
 *
 * The Reset Handler must be a Thumb address inside the actual image length. The
 * initial MSP must be 8-byte aligned and inside one caller-supplied SRAM region.
 */
firmware_status_t VectorValidation_Validate(
    const vector_table_values_t *vectors,
    const boot_region_t *app_region,
    uint32_t app_image_size,
    const memory_region_t *sram_regions,
    uint32_t sram_region_count);

#endif
