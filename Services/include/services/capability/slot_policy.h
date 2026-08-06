/**
 * @file slot_policy.h
 * @brief Fixed APP/GUI pair layout and inactive-slot selection policy.
 */
#ifndef SERVICES_SLOT_POLICY_H
#define SERVICES_SLOT_POLICY_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/boot_types.h"

#define SLOT_POLICY_FLASH_CAPACITY_BYTES (32UL * 1024UL * 1024UL)
#define SLOT_POLICY_ERASE_SIZE_BYTES      4096UL

/** Return the immutable APP/GUI layout for a release pair. */
firmware_status_t SlotPolicy_GetPairLayout(
    boot_pair_t pair,
    boot_pair_layout_t *layout);

/** Select the only pair that may be overwritten while active_pair is active. */
firmware_status_t SlotPolicy_SelectInactivePair(
    boot_pair_t active_pair,
    boot_pair_t *inactive_pair);

/** Check that a nonempty image fits entirely within a fixed region. */
firmware_status_t SlotPolicy_ValidateImageSize(
    const boot_region_t *region,
    uint32_t image_size);

/** Check an arbitrary nonempty flash range against a fixed region. */
int SlotPolicy_ContainsRange(
    const boot_region_t *region,
    uint32_t flash_offset,
    uint32_t size);

/** Validate that the detected storage geometry matches the product layout. */
firmware_status_t SlotPolicy_ValidateStorageGeometry(
    uint32_t capacity_bytes,
    uint32_t erase_size);

#endif
