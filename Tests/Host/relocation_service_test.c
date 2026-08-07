/**
 * @file relocation_service_test.c
 * @brief Host tests for HMI_RELOC_V1 decoding and streaming relocation.
 */
#include <stdint.h>
#include <stdio.h>

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

static int TestDecodeRelocation(void)
{
    uint8_t bytes[HMI_RELOCATION_ENTRY_SIZE] = {0};
    hmi_relocation_entry_t entry;

    WriteU32(&bytes[0], 0x1234U);
    WriteU16(&bytes[4], HMI_RELOCATION_ABS32_ADD_XIP_BASE);
    TEST_ASSERT(
        RelocationService_DecodeEntry(bytes, &entry) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((entry.target_offset == 0x1234U) &&
                (entry.type == HMI_RELOCATION_ABS32_ADD_XIP_BASE) &&
                (entry.reserved == 0U));
    return 0;
}

static int TestRelocation(void)
{
    relocation_service_t service;
    uint8_t first_block[32] = {0};
    uint8_t second_block[32] = {0};
    hmi_relocation_entry_t first_entry = {
        4U, HMI_RELOCATION_ABS32_ADD_XIP_BASE, 0U};
    hmi_relocation_entry_t second_entry = {
        36U, HMI_RELOCATION_ABS32_ADD_XIP_BASE, 0U};

    WriteU32(&first_block[4U], 0x90000101UL);
    WriteU32(&second_block[4U], 0x90000201UL);
    TEST_ASSERT(
        RelocationService_InitEx(
            &service, 64U, 0x90000000UL, 0x90A00000UL) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, first_block, sizeof(first_block), &first_entry, 1U) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(ReadU32(&first_block[4U]) == 0x90A00101UL);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service,
            32U,
            second_block,
            sizeof(second_block),
            &second_entry,
            1U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(ReadU32(&second_block[4U]) == 0x90A00201UL);
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
    hmi_relocation_entry_t entries[2] = {
        {4U, HMI_RELOCATION_ABS32_ADD_XIP_BASE, 0U},
        {4U, HMI_RELOCATION_ABS32_ADD_XIP_BASE, 0U},
    };

    WriteU32(&block[4U], 0x90000101UL);
    TEST_ASSERT(
        RelocationService_InitEx(
            &service, sizeof(block), 0x90000000UL, 0x90A00000UL) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 2U) ==
        FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(ReadU32(&block[4U]) == 0x90000101UL);
    TEST_ASSERT(service.next_block_offset == 0U);

    entries[0].target_offset = 0U;
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 1U) ==
        FIRMWARE_STATUS_INVALID_STATE);
    entries[0].target_offset = 4U;
    WriteU32(&block[4U], 0x8FFFFFFFUL);
    TEST_ASSERT(
        RelocationService_ApplyBlock(
            &service, 0U, block, sizeof(block), entries, 1U) ==
        FIRMWARE_STATUS_OUT_OF_RANGE);
    TEST_ASSERT(ReadU32(&block[4U]) == 0x8FFFFFFFUL);
    return 0;
}

int main(void)
{
    TEST_ASSERT(TestDecodeRelocation() == 0);
    TEST_ASSERT(TestRelocation() == 0);
    TEST_ASSERT(TestRelocationRejectionIsAtomic() == 0);
    printf("relocation_service_test: PASS\n");
    return 0;
}
