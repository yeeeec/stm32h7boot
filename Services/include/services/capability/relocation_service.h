/**
 * @file relocation_service.h
 * @brief Incremental APPX relocation validation and application.
 */
#ifndef SERVICES_RELOCATION_SERVICE_H
#define SERVICES_RELOCATION_SERVICE_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/appx_types.h"

/** State retained while canonical APP blocks are processed in order. */
typedef struct
{
    uint32_t image_size;
    uint32_t target_xip_base;
    uint32_t next_block_offset;
    uint32_t last_relocation_offset;
    uint32_t processed_relocations;
    int has_relocation;
    int initialized;
} relocation_service_t;

/**
 * @brief Initialize one streaming relocation operation.
 *
 * @param[out] service Service state to initialize.
 * @param[in] image_size Canonical APP image size in bytes.
 * @param[in] target_xip_base Fixed target slot CPU address.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for NULL or zero inputs.
 */
firmware_status_t RelocationService_Init(
    relocation_service_t *service,
    uint32_t image_size,
    uint32_t target_xip_base);

/**
 * @brief Validate and apply all relocation entries belonging to one APP block.
 *
 * Blocks must cover the image contiguously from offset zero. Entries must be
 * globally sorted and each entry must fall entirely within this block.
 *
 * @param[in,out] service Initialized stream state.
 * @param[in] block_offset Canonical image offset of @p data.
 * @param[in,out] data Mutable block receiving relocated little-endian words.
 * @param[in] data_size Nonzero block size in bytes.
 * @param[in] entries Entries for this block in ascending target-offset order.
 * @param[in] entry_count Number of entries; zero permits @p entries to be NULL.
 *
 * @return FIRMWARE_STATUS_OK when the block is accepted.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for invalid pointers or sizes.
 * @return FIRMWARE_STATUS_INVALID_STATE for ordering, type, or reserved-field errors.
 * @return FIRMWARE_STATUS_OUT_OF_RANGE for an entry or addition outside its range.
 */
firmware_status_t RelocationService_ApplyBlock(
    relocation_service_t *service,
    uint32_t block_offset,
    uint8_t *data,
    uint32_t data_size,
    const appx_relocation_entry_t *entries,
    uint32_t entry_count);

/**
 * @brief Confirm that the full image and expected relocation table were consumed.
 *
 * @param[in] service Initialized stream state.
 * @param[in] expected_relocations Relocation count declared by the APPX header.
 *
 * @return FIRMWARE_STATUS_OK when processing is complete.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if @p service is NULL.
 * @return FIRMWARE_STATUS_INVALID_STATE if image bytes or entries remain.
 */
firmware_status_t RelocationService_Finish(
    const relocation_service_t *service,
    uint32_t expected_relocations);

#endif
