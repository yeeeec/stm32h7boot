/**
 * @file appx_relocation_test.c
 * @brief Host tests for APPX header validation and streaming relocation.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "checksum/crc32_iso_hdlc.h"
#include "services/capability/appx_validation.h"
#include "services/capability/relocation_service.h"

#define TEST_ASSERT(condition)                                                \
    do                                                                        \
    {                                                                         \
        if (!(condition))                                                     \
        {                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int FinalizeHeaderCrc(uint8_t header[APPX_HEADER_SIZE])
{
    crc32_iso_hdlc_t crc;
    const checksum_t *checksum;
    uint32_t value;

    WriteU32(&header[0x2CU], 0U);
    TEST_ASSERT(Crc32IsoHdlc_Init(&crc) == FIRMWARE_STATUS_OK);
    checksum = Crc32IsoHdlc_Interface(&crc);
    TEST_ASSERT(checksum->reset(checksum->context) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(checksum->context, header, APPX_HEADER_SIZE) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->get_value(checksum->context, &value) == FIRMWARE_STATUS_OK);
    WriteU32(&header[0x2CU], value);
    return 0;
}

static int BuildHeader(uint8_t header[APPX_HEADER_SIZE])
{
    memset(header, 0, APPX_HEADER_SIZE);
    WriteU32(&header[0x00U], 0x58504148UL);
    WriteU16(&header[0x04U], 1U);
    WriteU16(&header[0x06U], APPX_HEADER_SIZE);
    WriteU32(&header[0x08U], 0U);
    WriteU32(&header[0x0CU], 64U);
    WriteU32(&header[0x10U], 0U);
    WriteU32(&header[0x14U], 4U);
    WriteU32(&header[0x18U], 128U);
    WriteU32(&header[0x1CU], 2U);
    WriteU16(&header[0x20U], APPX_RELOCATION_ENTRY_SIZE);
    WriteU16(&header[0x22U], 0U);
    WriteU32(&header[0x24U], 0x12345678UL);
    WriteU32(&header[0x28U], 0x87654321UL);
    return FinalizeHeaderCrc(header);
}

static int TestHeader(void)
{
    uint8_t bytes[APPX_HEADER_SIZE];
    crc32_iso_hdlc_t crc;
    appx_header_t header;

    TEST_ASSERT(BuildHeader(bytes) == 0);
    TEST_ASSERT(Crc32IsoHdlc_Init(&crc) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        AppxValidation_ParseHeader(
            Crc32IsoHdlc_Interface(&crc),
            bytes,
            144U,
            1024U,
            16U,
            &header) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((header.image_size == 64U) &&
                (header.relocation_offset == 128U) &&
                (header.relocation_count == 2U));

    bytes[0x30U] = 1U;
    TEST_ASSERT(
        AppxValidation_ParseHeader(
            Crc32IsoHdlc_Interface(&crc),
            bytes,
            144U,
            1024U,
            16U,
            &header) == FIRMWARE_STATUS_INVALID_STATE);
    bytes[0x30U] = 0U;

    bytes[0x2CU] ^= 1U;
    TEST_ASSERT(
        AppxValidation_ParseHeader(
            Crc32IsoHdlc_Interface(&crc),
            bytes,
            144U,
            1024U,
            16U,
            &header) == FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(BuildHeader(bytes) == 0);
    TEST_ASSERT(
        AppxValidation_ParseHeader(
            Crc32IsoHdlc_Interface(&crc),
            bytes,
            144U,
            1024U,
            1U,
            &header) == FIRMWARE_STATUS_OUT_OF_RANGE);
    return 0;
}

static int TestRelocation(void)
{
    relocation_service_t service;
    uint8_t first_block[32] = {0};
    uint8_t second_block[32] = {0};
    appx_relocation_entry_t first_entry = {
        4U, APPX_RELOCATION_ABS32_ADD_XIP_BASE, 0U};
    appx_relocation_entry_t second_entry = {
        36U, APPX_RELOCATION_ABS32_ADD_XIP_BASE, 0U};

    WriteU32(&first_block[4U], 0x101U);
    WriteU32(&second_block[4U], 0x201U);
    TEST_ASSERT(
        RelocationService_Init(&service, 64U, 0x90000000UL) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, first_block, sizeof(first_block), &first_entry, 1U) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(ReadU32(&first_block[4U]) == 0x90000101UL);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service,
            32U,
            second_block,
            sizeof(second_block),
            &second_entry,
            1U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(ReadU32(&second_block[4U]) == 0x90000201UL);
    TEST_ASSERT(RelocationService_Finish(&service, 2U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        RelocationService_Finish(&service, 3U) ==
        FIRMWARE_STATUS_INVALID_STATE);
    return 0;
}

static int TestRelocationRejectionIsAtomic(void)
{
    relocation_service_t service;
    uint8_t block[16] = {0};
    appx_relocation_entry_t entries[2] = {
        {4U, APPX_RELOCATION_ABS32_ADD_XIP_BASE, 0U},
        {4U, APPX_RELOCATION_ABS32_ADD_XIP_BASE, 0U},
    };

    WriteU32(&block[4U], 0x101U);
    TEST_ASSERT(
        RelocationService_Init(&service, sizeof(block), 0x90000000UL) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 2U) ==
        FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(ReadU32(&block[4U]) == 0x101U);
    TEST_ASSERT(service.next_block_offset == 0U);

    entries[0].target_offset = 0U;
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 1U) ==
        FIRMWARE_STATUS_INVALID_STATE);
    entries[0].target_offset = 4U;
    WriteU32(&block[4U], UINT32_MAX);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 1U) ==
        FIRMWARE_STATUS_OUT_OF_RANGE);
    TEST_ASSERT(ReadU32(&block[4U]) == UINT32_MAX);
    return 0;
}

static int TestDecodeRelocation(void)
{
    uint8_t bytes[APPX_RELOCATION_ENTRY_SIZE] = {0};
    appx_relocation_entry_t entry;

    WriteU32(&bytes[0], 0x1234U);
    WriteU16(&bytes[4], APPX_RELOCATION_ABS32_ADD_XIP_BASE);
    TEST_ASSERT(
        AppxValidation_DecodeRelocation(bytes, &entry) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((entry.target_offset == 0x1234U) &&
                (entry.type == APPX_RELOCATION_ABS32_ADD_XIP_BASE) &&
                (entry.reserved == 0U));
    return 0;
}

int main(void)
{
    TEST_ASSERT(TestHeader() == 0);
    TEST_ASSERT(TestRelocation() == 0);
    TEST_ASSERT(TestRelocationRejectionIsAtomic() == 0);
    TEST_ASSERT(TestDecodeRelocation() == 0);
    printf("appx_relocation_test: PASS\n");
    return 0;
}
