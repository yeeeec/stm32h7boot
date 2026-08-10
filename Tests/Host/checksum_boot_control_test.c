/**
 * @file checksum_boot_control_test.c
 * @brief Host tests for CRC32 and power-loss-safe Boot Control persistence.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "checksum/crc32_iso_hdlc.h"
#include "services/capability/boot_control_service.h"

#define STORE_CAPACITY         0x0400U
#define STORE_PAGE_SIZE        16U
#define ACTIVE_A_ADDRESS       0x0000U
#define ACTIVE_B_ADDRESS       0x0100U
#define ACTIVE_CRC_OFFSET      0x00F8U
#define ACTIVE_MARKER_OFFSET   0x00FCU
#define ACTIVE_WRITE_COUNT     18U
#define COMMIT_MARKER          0x434F4D54UL
#define NO_WRITE_FAILURE       0U

#define TEST_ASSERT(condition)                                                \
    do                                                                        \
    {                                                                         \
        if (!(condition))                                                     \
        {                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

typedef struct
{
    boot_control_store_t interface;
    uint8_t bytes[STORE_CAPACITY];
    uint32_t write_attempts;
    uint32_t accepted_writes;
    uint32_t fail_on_attempt;
    int crossed_page;
} fake_store_t;

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static firmware_status_t StoreGetInfo(
    void *context,
    boot_control_store_info_t *info)
{
    (void)context;
    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    info->capacity_bytes = STORE_CAPACITY;
    info->page_size = STORE_PAGE_SIZE;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StoreRead(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    fake_store_t *store = (fake_store_t *)context;

    if ((store == NULL) || (data == NULL) ||
        (address > STORE_CAPACITY) || (size > (STORE_CAPACITY - address)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(data, &store->bytes[address], size);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StoreWritePage(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    fake_store_t *store = (fake_store_t *)context;
    uint32_t page_offset;

    if ((store == NULL) || (data == NULL) || (size == 0U) ||
        (address >= STORE_CAPACITY) || (size > (STORE_CAPACITY - address)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    page_offset = address % STORE_PAGE_SIZE;
    if (size > (STORE_PAGE_SIZE - page_offset))
    {
        store->crossed_page = 1;
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    ++store->write_attempts;
    if ((store->fail_on_attempt != NO_WRITE_FAILURE) &&
        (store->write_attempts == store->fail_on_attempt))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    memcpy(&store->bytes[address], data, size);
    ++store->accepted_writes;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StoreIsReady(void *context, int *ready)
{
    if ((context == NULL) || (ready == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *ready = 1;
    return FIRMWARE_STATUS_OK;
}

static void StoreInit(fake_store_t *store)
{
    memset(store, 0, sizeof(*store));
    memset(store->bytes, 0xFF, sizeof(store->bytes));
    store->interface.context = store;
    store->interface.get_info = StoreGetInfo;
    store->interface.read = StoreRead;
    store->interface.write_page = StoreWritePage;
    store->interface.is_ready = StoreIsReady;
}

static void StoreResetCounters(fake_store_t *store)
{
    store->write_attempts = 0U;
    store->accepted_writes = 0U;
    store->fail_on_attempt = NO_WRITE_FAILURE;
    store->crossed_page = 0;
}

static int ServiceInit(
    boot_control_service_t *service,
    crc32_iso_hdlc_t *crc,
    fake_store_t *store)
{
    boot_control_service_dependencies_t dependencies;

    memset(service, 0, sizeof(*service));
    TEST_ASSERT(Crc32IsoHdlc_Init(crc) == FIRMWARE_STATUS_OK);
    dependencies.store = &store->interface;
    dependencies.checksum = Crc32IsoHdlc_Interface(crc);
    TEST_ASSERT(
        BootControlService_Init(service, &dependencies) == FIRMWARE_STATUS_OK);
    return 0;
}

static boot_active_record_t ActiveRecord(uint32_t tag)
{
    boot_active_record_t record;
    uint32_t index;

    memset(&record, 0, sizeof(record));
    record.format_version = BOOT_ACTIVE_RECORD_FORMAT_V2;
    record.state = BOOT_ACTIVE_RECORD_STATE_VALID;
    record.flags = (uint8_t)tag;
    record.release_version.major = 1U;
    record.release_version.minor = (uint16_t)tag;
    record.release_version.patch = 3U;
    record.build_number = 100U + tag;
    record.app_size = 0x10000U;
    record.gui_size = 0x20000U;
    for (index = 0U; index < BOOT_CONTROL_PACKAGE_ID_HASH_SIZE; ++index)
    {
        record.package_id_hash[index] = (uint8_t)(tag + index);
    }
    for (index = 0U; index < BOOT_CONTROL_MANIFEST_HASH_SIZE; ++index)
    {
        record.manifest_sha256[index] = (uint8_t)(tag + index + 0x40U);
    }
    for (index = 0U; index < BOOT_CONTROL_IMAGE_HASH_SIZE; ++index)
    {
        record.app_sha256[index] = (uint8_t)(tag + index + 0x60U);
        record.gui_sha256[index] = (uint8_t)(tag + index + 0x80U);
    }
    return record;
}

static int RunCommit(boot_control_service_t *service)
{
    uint32_t iteration;

    for (iteration = 0U; iteration < 200U; ++iteration)
    {
        service_run_state_t state = BootControlService_GetState(service);

        if ((state == SERVICE_RUN_STATE_SUCCEEDED) ||
            (state == SERVICE_RUN_STATE_FAILED))
        {
            return 0;
        }
        BootControlService_Process(service);
    }
    return 1;
}

static int CommitActive(
    boot_control_service_t *service,
    const boot_active_record_t *record)
{
    TEST_ASSERT(
        BootControlService_CommitActiveStart(service, record) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(RunCommit(service) == 0);
    TEST_ASSERT(
        BootControlService_GetState(service) == SERVICE_RUN_STATE_SUCCEEDED);
    return 0;
}

static int RecalculateActiveCrc(uint8_t *record_bytes)
{
    crc32_iso_hdlc_t crc;
    const checksum_t *checksum;
    uint32_t value;

    TEST_ASSERT(Crc32IsoHdlc_Init(&crc) == FIRMWARE_STATUS_OK);
    checksum = Crc32IsoHdlc_Interface(&crc);
    TEST_ASSERT(checksum->reset(checksum->context) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(
            checksum->context, record_bytes, ACTIVE_CRC_OFFSET) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->get_value(checksum->context, &value) == FIRMWARE_STATUS_OK);
    WriteU32(&record_bytes[ACTIVE_CRC_OFFSET], value);
    return 0;
}

static int TestCrc32(void)
{
    static const uint8_t vector[] = "123456789";
    crc32_iso_hdlc_t crc;
    const checksum_t *checksum;
    uint32_t value;

    TEST_ASSERT(Crc32IsoHdlc_Init(&crc) == FIRMWARE_STATUS_OK);
    checksum = Crc32IsoHdlc_Interface(&crc);
    TEST_ASSERT(checksum->reset(checksum->context) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(checksum->context, vector, sizeof(vector) - 1U) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->get_value(checksum->context, &value) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(value == 0xCBF43926UL);

    TEST_ASSERT(checksum->reset(checksum->context) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(checksum->context, vector, 4U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(checksum->context, NULL, 0U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->update(checksum->context, &vector[4], 5U) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        checksum->get_value(checksum->context, &value) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(value == 0xCBF43926UL);
    return 0;
}

static int TestFirstCommitAndSelection(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t pair_1 = ActiveRecord(1U);
    boot_active_record_t pair_2 = ActiveRecord(2U);
    boot_active_record_t loaded;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) ==
        FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(CommitActive(&service, &pair_1) == 0);
    TEST_ASSERT(store.crossed_page == 0);
    TEST_ASSERT(store.accepted_writes == ACTIVE_WRITE_COUNT);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_A_ADDRESS + ACTIVE_MARKER_OFFSET]) ==
                COMMIT_MARKER);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_A_ADDRESS + 0x00U]) == 0x52434248UL);
    TEST_ASSERT(ReadU16(&store.bytes[ACTIVE_A_ADDRESS + 0x04U]) ==
                BOOT_ACTIVE_RECORD_FORMAT_V2);
    TEST_ASSERT(ReadU16(&store.bytes[ACTIVE_A_ADDRESS + 0x06U]) == BOOT_ACTIVE_RECORD_SIZE);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_A_ADDRESS + 0x1CU]) == pair_1.app_size);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_A_ADDRESS + 0x20U]) == pair_1.gui_size);
    TEST_ASSERT(memcmp(&store.bytes[ACTIVE_A_ADDRESS + 0x54U], pair_1.app_sha256,
                       BOOT_CONTROL_IMAGE_HASH_SIZE) == 0);
    TEST_ASSERT(memcmp(&store.bytes[ACTIVE_A_ADDRESS + 0x74U], pair_1.gui_sha256,
                       BOOT_CONTROL_IMAGE_HASH_SIZE) == 0);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((loaded.sequence == 1U) &&
                (loaded.format_version == BOOT_ACTIVE_RECORD_FORMAT_V2) &&
                (loaded.state == BOOT_ACTIVE_RECORD_STATE_VALID) && (loaded.flags == 1U));
    StoreResetCounters(&store);
    TEST_ASSERT(CommitActive(&service, &pair_2) == 0);
    TEST_ASSERT(store.accepted_writes == ACTIVE_WRITE_COUNT);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_B_ADDRESS + ACTIVE_MARKER_OFFSET]) ==
                COMMIT_MARKER);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((loaded.sequence == 2U) && (loaded.flags == 2U));
    TEST_ASSERT(memcmp(loaded.app_sha256, pair_2.app_sha256, sizeof(loaded.app_sha256)) == 0);
    TEST_ASSERT(memcmp(loaded.gui_sha256, pair_2.gui_sha256, sizeof(loaded.gui_sha256)) == 0);
    return 0;
}

static int TestSequenceWraparound(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t pair_1 = ActiveRecord(1U);
    boot_active_record_t pair_2 = ActiveRecord(2U);
    boot_active_record_t loaded;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &pair_1) == 0);
    WriteU32(&store.bytes[ACTIVE_A_ADDRESS + 0x08U], UINT32_MAX);
    TEST_ASSERT(RecalculateActiveCrc(&store.bytes[ACTIVE_A_ADDRESS]) == 0);
    TEST_ASSERT(CommitActive(&service, &pair_2) == 0);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((loaded.sequence == 0U) && (loaded.flags == 2U));
    return 0;
}

static int TestTherapyV3RoundTrip(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t record = ActiveRecord(7U);
    boot_active_record_t loaded;
    uint32_t index;

    record.format_version = BOOT_ACTIVE_RECORD_FORMAT_V3;
    record.component_mask = UPDATE_COMPONENT_ALL;
    record.therapy_size = BOOT_CONTROL_THERAPY_MAX_SIZE;
    record.therapy_version.major = 3U;
    record.therapy_version.minor = 2U;
    record.therapy_version.patch = 1U;
    for (index = 0U; index < sizeof(record.therapy_sha256); ++index)
    {
        record.therapy_sha256[index] = (uint8_t)(0xC0U + index);
    }
    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &record) == 0);
    TEST_ASSERT(ReadU16(&store.bytes[ACTIVE_A_ADDRESS + 0x04U]) ==
                BOOT_ACTIVE_RECORD_FORMAT_V3);
    TEST_ASSERT(store.bytes[ACTIVE_A_ADDRESS + 0x94U] == UPDATE_COMPONENT_ALL);
    TEST_ASSERT(ReadU32(&store.bytes[ACTIVE_A_ADDRESS + 0x98U]) ==
                BOOT_CONTROL_THERAPY_MAX_SIZE);
    TEST_ASSERT(BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((loaded.component_mask == UPDATE_COMPONENT_ALL) &&
                (loaded.therapy_size == BOOT_CONTROL_THERAPY_MAX_SIZE) &&
                (loaded.therapy_version.major == 3U) &&
                (loaded.therapy_version.minor == 2U) &&
                (loaded.therapy_version.patch == 1U));
    TEST_ASSERT(memcmp(loaded.therapy_sha256, record.therapy_sha256,
                       sizeof(loaded.therapy_sha256)) == 0);
    return 0;
}

static int TestEqualSequenceRecords(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t record = ActiveRecord(1U);
    boot_active_record_t loaded;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &record) == 0);
    memcpy(
        &store.bytes[ACTIVE_B_ADDRESS],
        &store.bytes[ACTIVE_A_ADDRESS],
        256U);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(loaded.sequence == 1U);

    store.bytes[ACTIVE_B_ADDRESS + 0x20U] ^= 1U;
    TEST_ASSERT(RecalculateActiveCrc(&store.bytes[ACTIVE_B_ADDRESS]) == 0);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) ==
        FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(
        BootControlService_CommitActiveStart(&service, &record) ==
        FIRMWARE_STATUS_INVALID_STATE);
    return 0;
}

static int TestAmbiguousSequenceRecords(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t record = ActiveRecord(1U);
    boot_active_record_t loaded;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &record) == 0);
    memcpy(
        &store.bytes[ACTIVE_B_ADDRESS],
        &store.bytes[ACTIVE_A_ADDRESS],
        256U);
    WriteU32(&store.bytes[ACTIVE_B_ADDRESS + 0x08U], 0x80000001UL);
    TEST_ASSERT(RecalculateActiveCrc(&store.bytes[ACTIVE_B_ADDRESS]) == 0);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) ==
        FIRMWARE_STATUS_INVALID_STATE);
    return 0;
}

static int TestCorruptRecordFallback(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t pair_1 = ActiveRecord(1U);
    boot_active_record_t pair_2 = ActiveRecord(2U);
    boot_active_record_t loaded;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &pair_1) == 0);
    TEST_ASSERT(CommitActive(&service, &pair_2) == 0);

    store.bytes[ACTIVE_B_ADDRESS + ACTIVE_CRC_OFFSET] ^= 1U;
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(loaded.flags == 1U);

    WriteU32(
        &store.bytes[ACTIVE_A_ADDRESS + ACTIVE_MARKER_OFFSET], UINT32_MAX);
    TEST_ASSERT(
        BootControlService_LoadActive(&service, &loaded) ==
        FIRMWARE_STATUS_INVALID_STATE);
    return 0;
}

static int TestAcceptedWritePowerLoss(void)
{
    uint32_t cutoff;

    for (cutoff = 0U; cutoff <= ACTIVE_WRITE_COUNT; ++cutoff)
    {
        fake_store_t store;
        boot_control_service_t service;
        boot_control_service_t rebooted_service;
        crc32_iso_hdlc_t crc;
        crc32_iso_hdlc_t rebooted_crc;
        boot_active_record_t pair_1 = ActiveRecord(1U);
        boot_active_record_t pair_2 = ActiveRecord(2U);
        boot_active_record_t loaded;
        uint32_t iteration;

        StoreInit(&store);
        TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
        TEST_ASSERT(CommitActive(&service, &pair_1) == 0);
        StoreResetCounters(&store);
        TEST_ASSERT(
            BootControlService_CommitActiveStart(&service, &pair_2) ==
            FIRMWARE_STATUS_OK);

        for (iteration = 0U;
             (iteration < 200U) && (store.accepted_writes < cutoff);
             ++iteration)
        {
            BootControlService_Process(&service);
        }
        TEST_ASSERT(store.accepted_writes == cutoff);

        TEST_ASSERT(
            ServiceInit(&rebooted_service, &rebooted_crc, &store) == 0);
        TEST_ASSERT(
            BootControlService_LoadActive(&rebooted_service, &loaded) ==
            FIRMWARE_STATUS_OK);
        TEST_ASSERT(loaded.flags == ((cutoff == ACTIVE_WRITE_COUNT) ? 2U : 1U));
    }
    return 0;
}

static int TestWriteFailureInjection(void)
{
    uint32_t failure;

    for (failure = 1U; failure <= ACTIVE_WRITE_COUNT; ++failure)
    {
        fake_store_t store;
        boot_control_service_t service;
        boot_control_service_t rebooted_service;
        crc32_iso_hdlc_t crc;
        crc32_iso_hdlc_t rebooted_crc;
        boot_active_record_t pair_1 = ActiveRecord(1U);
        boot_active_record_t pair_2 = ActiveRecord(2U);
        boot_active_record_t loaded;

        StoreInit(&store);
        TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
        TEST_ASSERT(CommitActive(&service, &pair_1) == 0);
        StoreResetCounters(&store);
        store.fail_on_attempt = failure;
        TEST_ASSERT(
            BootControlService_CommitActiveStart(&service, &pair_2) ==
            FIRMWARE_STATUS_OK);
        TEST_ASSERT(RunCommit(&service) == 0);
        TEST_ASSERT(
            BootControlService_GetState(&service) == SERVICE_RUN_STATE_FAILED);

        TEST_ASSERT(
            ServiceInit(&rebooted_service, &rebooted_crc, &store) == 0);
        TEST_ASSERT(
            BootControlService_LoadActive(&rebooted_service, &loaded) ==
            FIRMWARE_STATUS_OK);
        TEST_ASSERT(loaded.flags == 1U);
    }
    return 0;
}

static firmware_status_t FailingChecksumReset(void *context)
{
    return (context == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                             : FIRMWARE_STATUS_OK;
}

static firmware_status_t FailingChecksumUpdate(
    void *context,
    const void *data,
    size_t size)
{
    (void)data;
    (void)size;
    return (context == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                             : FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t FailingChecksumGetValue(
    void *context,
    uint32_t *value)
{
    (void)value;
    return (context == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                             : FIRMWARE_STATUS_OK;
}

static int TestChecksumFailurePropagation(void)
{
    fake_store_t store;
    boot_control_service_t service;
    boot_control_service_t failing_service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t record = ActiveRecord(1U);
    boot_active_record_t loaded;
    boot_control_service_dependencies_t dependencies;
    checksum_t failing_checksum;
    uint32_t checksum_context = 1U;

    StoreInit(&store);
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(CommitActive(&service, &record) == 0);

    failing_checksum.context = &checksum_context;
    failing_checksum.reset = FailingChecksumReset;
    failing_checksum.update = FailingChecksumUpdate;
    failing_checksum.get_value = FailingChecksumGetValue;
    dependencies.store = &store.interface;
    dependencies.checksum = &failing_checksum;
    memset(&failing_service, 0, sizeof(failing_service));
    TEST_ASSERT(
        BootControlService_Init(&failing_service, &dependencies) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        BootControlService_LoadActive(&failing_service, &loaded) ==
        FIRMWARE_STATUS_IO_ERROR);
    return 0;
}

static int TestV1Unsupported(void)
{
    fake_store_t store;
    boot_control_service_t service;
    crc32_iso_hdlc_t crc;
    boot_active_record_t loaded;

    StoreInit(&store);
    WriteU32(&store.bytes[ACTIVE_A_ADDRESS + 0x00U], 0x52434248UL);
    store.bytes[ACTIVE_A_ADDRESS + 0x04U] = 1U;
    store.bytes[ACTIVE_A_ADDRESS + 0x05U] = 0U;
    TEST_ASSERT(ServiceInit(&service, &crc, &store) == 0);
    TEST_ASSERT(BootControlService_LoadActive(&service, &loaded) ==
                FIRMWARE_STATUS_NOT_SUPPORTED);
    return 0;
}

int main(void)
{
    TEST_ASSERT(TestCrc32() == 0);
    TEST_ASSERT(TestFirstCommitAndSelection() == 0);
    TEST_ASSERT(TestSequenceWraparound() == 0);
    TEST_ASSERT(TestEqualSequenceRecords() == 0);
    TEST_ASSERT(TestAmbiguousSequenceRecords() == 0);
    TEST_ASSERT(TestCorruptRecordFallback() == 0);
    TEST_ASSERT(TestAcceptedWritePowerLoss() == 0);
    TEST_ASSERT(TestWriteFailureInjection() == 0);
    TEST_ASSERT(TestChecksumFailurePropagation() == 0);
    TEST_ASSERT(TestV1Unsupported() == 0);
    TEST_ASSERT(TestTherapyV3RoundTrip() == 0);
    printf("checksum_boot_control_test: PASS\n");
    return 0;
}
