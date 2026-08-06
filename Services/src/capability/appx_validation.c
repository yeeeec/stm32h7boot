/**
 * @file appx_validation.c
 * @brief HMI_XIP_APP_V1 format validation implementation.
 */
#include "services/capability/appx_validation.h"

#include <stddef.h>

#include "services/capability/checked_arithmetic.h"

#define APPX_MAGIC              0x58504148UL
#define APPX_FORMAT_VERSION     1U
#define APPX_CANONICAL_BASE     0U
#define APPX_HEADER_CRC_OFFSET  0x2CU
#define APPX_RESERVED_OFFSET    0x30U

static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static firmware_status_t CalculateHeaderCrc(
    const checksum_t *checksum,
    const uint8_t *bytes,
    uint32_t *value)
{
    static const uint8_t zero_crc[4] = {0U, 0U, 0U, 0U};
    firmware_status_t status = checksum->reset(checksum->context);

    if (FirmwareStatus_IsOk(status))
    {
        status = checksum->update(
            checksum->context, bytes, APPX_HEADER_CRC_OFFSET);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = checksum->update(
            checksum->context, zero_crc, sizeof(zero_crc));
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = checksum->update(
            checksum->context,
            &bytes[APPX_RESERVED_OFFSET],
            APPX_HEADER_SIZE - APPX_RESERVED_OFFSET);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = checksum->get_value(checksum->context, value);
    }
    return status;
}

firmware_status_t AppxValidation_ParseHeader(
    const checksum_t *checksum,
    const uint8_t bytes[APPX_HEADER_SIZE],
    uint32_t file_size,
    uint32_t maximum_image_size,
    uint32_t maximum_relocations,
    appx_header_t *header)
{
    appx_header_t parsed;
    uint32_t relocation_size;
    uint32_t expected_file_size;
    uint32_t actual_header_crc;
    uint32_t index;
    firmware_status_t status;

    if ((checksum == NULL) || (checksum->reset == NULL) ||
        (checksum->update == NULL) || (checksum->get_value == NULL) ||
        (bytes == NULL) || (maximum_image_size == 0U) ||
        (maximum_relocations == 0U) || (header == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((ReadU32(&bytes[0x00U]) != APPX_MAGIC) ||
        (ReadU16(&bytes[0x04U]) != APPX_FORMAT_VERSION) ||
        (ReadU16(&bytes[0x06U]) != APPX_HEADER_SIZE) ||
        (ReadU32(&bytes[0x08U]) != APPX_CANONICAL_BASE) ||
        (ReadU16(&bytes[0x20U]) != APPX_RELOCATION_ENTRY_SIZE) ||
        (ReadU16(&bytes[0x22U]) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = APPX_RESERVED_OFFSET; index < APPX_HEADER_SIZE; ++index)
    {
        if (bytes[index] != 0U)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }

    parsed.image_size = ReadU32(&bytes[0x0CU]);
    parsed.vector_offset = ReadU32(&bytes[0x10U]);
    parsed.entry_offset = ReadU32(&bytes[0x14U]);
    parsed.relocation_offset = ReadU32(&bytes[0x18U]);
    parsed.relocation_count = ReadU32(&bytes[0x1CU]);
    parsed.image_crc32 = ReadU32(&bytes[0x24U]);
    parsed.relocation_crc32 = ReadU32(&bytes[0x28U]);

    if ((parsed.image_size == 0U) ||
        (parsed.image_size > maximum_image_size) ||
        (parsed.vector_offset != 0U) ||
        (parsed.entry_offset >= parsed.image_size) ||
        ((parsed.entry_offset & 1U) != 0U) ||
        (parsed.relocation_count > maximum_relocations))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if (!CheckedArithmetic_MultiplyU32(
            parsed.relocation_count,
            APPX_RELOCATION_ENTRY_SIZE,
            &relocation_size) ||
        !CheckedArithmetic_AddU32(
            APPX_HEADER_SIZE, parsed.image_size, &expected_file_size) ||
        (parsed.relocation_offset != expected_file_size) ||
        ((parsed.relocation_offset & 3U) != 0U) ||
        !CheckedArithmetic_AddU32(
            parsed.relocation_offset,
            relocation_size,
            &expected_file_size) ||
        (expected_file_size != file_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    status = CalculateHeaderCrc(checksum, bytes, &actual_header_crc);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (actual_header_crc != ReadU32(&bytes[APPX_HEADER_CRC_OFFSET]))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    *header = parsed;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t AppxValidation_DecodeRelocation(
    const uint8_t bytes[APPX_RELOCATION_ENTRY_SIZE],
    appx_relocation_entry_t *entry)
{
    if ((bytes == NULL) || (entry == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    entry->target_offset = ReadU32(&bytes[0x00U]);
    entry->type = ReadU16(&bytes[0x04U]);
    entry->reserved = ReadU16(&bytes[0x06U]);
    return FIRMWARE_STATUS_OK;
}
