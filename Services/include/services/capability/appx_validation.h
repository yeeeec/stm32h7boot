/**
 * @file appx_validation.h
 * @brief HMI_XIP_APP_V1 header and relocation-entry validation.
 */
#ifndef SERVICES_APPX_VALIDATION_H
#define SERVICES_APPX_VALIDATION_H

#include <stdint.h>

#include "firmware/checksum.h"
#include "services/common/appx_types.h"

/**
 * @brief Parse and validate one fixed-size APPX V1 header.
 *
 * @param[in] checksum Exclusive checksum provider used for header CRC32.
 * @param[in] bytes Exactly APPX_HEADER_SIZE header bytes.
 * @param[in] file_size Complete APPX file size in bytes.
 * @param[in] maximum_image_size Product APP slot capacity in bytes.
 * @param[in] maximum_relocations Product relocation-entry limit.
 * @param[out] header Parsed header written only after full validation.
 *
 * @return FIRMWARE_STATUS_OK when all format and range invariants hold.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for NULL or zero configuration.
 * @return FIRMWARE_STATUS_INVALID_STATE for a malformed header or bad CRC.
 * @return FIRMWARE_STATUS_OUT_OF_RANGE for unsupported image or table geometry.
 * @return A checksum-provider failure otherwise.
 */
firmware_status_t AppxValidation_ParseHeader(
    const checksum_t *checksum,
    const uint8_t bytes[APPX_HEADER_SIZE],
    uint32_t file_size,
    uint32_t maximum_image_size,
    uint32_t maximum_relocations,
    appx_header_t *header);

/**
 * @brief Decode one little-endian APPX V1 relocation entry.
 *
 * @param[in] bytes Exactly APPX_RELOCATION_ENTRY_SIZE bytes.
 * @param[out] entry Decoded entry.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if an argument is NULL.
 */
firmware_status_t AppxValidation_DecodeRelocation(
    const uint8_t bytes[APPX_RELOCATION_ENTRY_SIZE],
    appx_relocation_entry_t *entry);

#endif
