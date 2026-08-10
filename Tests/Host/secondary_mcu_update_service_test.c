/**
 * @file secondary_mcu_update_service_test.c
 * @brief 外部 MCU 安装 Service 的 Host 状态机测试。
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "firmware/image_source.h"
#include "firmware/mcu_programmer.h"
#include "services/use_case/secondary_mcu_update_service.h"

#define IMAGE_SIZE 517U
#define TARGET_BASE 0x08010000UL
#define TARGET_CAPACITY 0x00001000UL

typedef struct
{
    uint8_t bytes[IMAGE_SIZE];
    uint32_t read_count;
} source_fixture_t;

typedef struct
{
    uint8_t target[TARGET_CAPACITY];
    uint32_t target_base;
    uint32_t max_write_size;
    uint32_t max_read_size;
    uint32_t max_erase_count;
    uint32_t begin_count;
    uint32_t erase_count;
    uint32_t write_count;
    uint32_t read_count;
    uint32_t abort_count;
    uint32_t end_count;
    int active;
    int corrupt_readback;
} programmer_fixture_t;

typedef struct
{
    uint32_t value;
} hash_fixture_t;

static firmware_status_t HashReset(void *context)
{
    ((hash_fixture_t *)context)->value = 2166136261UL;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashUpdate(void *context, const void *data, size_t size)
{
    hash_fixture_t *hash = (hash_fixture_t *)context;
    const uint8_t *bytes = (const uint8_t *)data;

    while (size-- != 0U)
    {
        hash->value = (hash->value ^ *bytes++) * 16777619UL;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashFinish(void *context,
                                    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    const hash_fixture_t *hash = (const hash_fixture_t *)context;
    uint32_t index;

    for (index = 0U; index < FIRMWARE_SHA256_DIGEST_SIZE; ++index)
    {
        digest[index] = (uint8_t)(hash->value >> ((index & 3U) * 8U));
    }
    return FIRMWARE_STATUS_OK;
}

static hash_provider_t MakeHash(hash_fixture_t *fixture)
{
    hash_provider_t hash = {fixture, HashReset, HashUpdate, HashFinish};
    return hash;
}

static firmware_status_t SourceGetInfo(
    void *context,
    firmware_image_info_t *info)
{
    source_fixture_t *source = (source_fixture_t *)context;

    if ((source == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    info->size_bytes = IMAGE_SIZE;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceRead(
    void *context,
    uint32_t offset,
    uint8_t *data,
    uint32_t size)
{
    source_fixture_t *source = (source_fixture_t *)context;

    if ((source == NULL) || (data == NULL) ||
        (offset > IMAGE_SIZE) || (size > (IMAGE_SIZE - offset)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(data, &source->bytes[offset], size);
    source->read_count++;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerBegin(
    void *context,
    mcu_programmer_info_t *info)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;

    if ((programmer == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    programmer->begin_count++;
    programmer->active = 1;
    info->device_id = 0x0450U;
    info->max_write_size = programmer->max_write_size;
    info->max_read_size = programmer->max_read_size;
    info->max_erase_block_count = programmer->max_erase_count;
    info->capabilities = MCU_PROGRAMMER_CAPABILITY_READ |
                         MCU_PROGRAMMER_CAPABILITY_WRITE |
                         MCU_PROGRAMMER_CAPABILITY_ERASE;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerErase(
    void *context,
    uint32_t page_start,
    uint32_t page_count)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;
    (void)page_start;
    if ((programmer == NULL) || (programmer->active == 0) ||
        (page_count == 0U) || (page_count > programmer->max_erase_count))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    memset(programmer->target, 0xFF, sizeof(programmer->target));
    programmer->erase_count++;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerWrite(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;
    uint32_t offset;

    if ((programmer == NULL) || (programmer->active == 0) || (data == NULL) ||
        (size == 0U) || (size > programmer->max_write_size) ||
        (address < programmer->target_base))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    offset = address - programmer->target_base;
    if ((offset > TARGET_CAPACITY) || (size > TARGET_CAPACITY - offset))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(&programmer->target[offset], data, size);
    programmer->write_count++;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerRead(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;
    uint32_t offset;

    if ((programmer == NULL) || (programmer->active == 0) || (data == NULL) ||
        (size == 0U) || (size > programmer->max_read_size) ||
        (address < programmer->target_base))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    offset = address - programmer->target_base;
    if ((offset > TARGET_CAPACITY) || (size > TARGET_CAPACITY - offset))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(data, &programmer->target[offset], size);
    if (programmer->corrupt_readback != 0)
    {
        data[0] ^= 0x01U;
    }
    programmer->read_count++;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerEnd(void *context)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;
    if ((programmer == NULL) || (programmer->active == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    programmer->active = 0;
    programmer->end_count++;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ProgrammerAbort(void *context)
{
    programmer_fixture_t *programmer = (programmer_fixture_t *)context;
    if (programmer == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    programmer->active = 0;
    programmer->abort_count++;
    return FIRMWARE_STATUS_OK;
}

static void FillFixture(
    source_fixture_t *source,
    programmer_fixture_t *programmer,
    firmware_image_source_t *image,
    mcu_programmer_t *target)
{
    uint32_t index;

    memset(source, 0, sizeof(*source));
    memset(programmer, 0, sizeof(*programmer));
    for (index = 0U; index < IMAGE_SIZE; ++index)
    {
        source->bytes[index] = (uint8_t)(index * 13U + 7U);
    }
    programmer->target_base = TARGET_BASE;
    programmer->max_write_size = 100U;
    programmer->max_read_size = 100U;
    programmer->max_erase_count = 2U;
    image->context = source;
    image->get_info = SourceGetInfo;
    image->read = SourceRead;
    target->context = programmer;
    target->begin = ProgrammerBegin;
    target->erase = ProgrammerErase;
    target->write = ProgrammerWrite;
    target->read = ProgrammerRead;
    target->end = ProgrammerEnd;
    target->abort = ProgrammerAbort;
}

static void RunToTerminal(secondary_mcu_update_service_t *service)
{
    uint32_t guard = 0U;
    while (SecondaryMcuUpdateService_GetState(service) ==
           SERVICE_RUN_STATE_RUNNING)
    {
        SecondaryMcuUpdateService_Process(service);
        assert(++guard < 200U);
    }
}

static secondary_mcu_update_request_t DefaultRequest(void);

static secondary_mcu_update_request_t DefaultRequest(void)
{
    secondary_mcu_update_request_t request = {
        TARGET_BASE,
        TARGET_CAPACITY,
        0U,
        5U,
        IMAGE_SIZE,
        {0U},
    };
    hash_fixture_t hash;
    uint8_t bytes[IMAGE_SIZE];
    uint32_t index;

    for (index = 0U; index < IMAGE_SIZE; ++index)
    {
        bytes[index] = (uint8_t)(index * 13U + 7U);
    }
    (void)HashReset(&hash);
    (void)HashUpdate(&hash, bytes, sizeof(bytes));
    (void)HashFinish(&hash, request.sha256);
    return request;
}

static void TestInstallAndReadback(void)
{
    source_fixture_t source;
    programmer_fixture_t programmer;
    firmware_image_source_t image = {0};
    mcu_programmer_t target = {0};
    secondary_mcu_update_service_t service = {0};
    uint8_t write_buffer[256];
    uint8_t readback_buffer[256];
    secondary_mcu_update_service_dependencies_t dependencies;
    secondary_mcu_update_request_t request;
    hash_fixture_t hash_fixture;
    hash_provider_t hash = MakeHash(&hash_fixture);

    FillFixture(&source, &programmer, &image, &target);
    dependencies.source = &image;
    dependencies.programmer = &target;
    dependencies.hash = &hash;
    dependencies.write_buffer = write_buffer;
    dependencies.readback_buffer = readback_buffer;
    dependencies.buffer_size = sizeof(write_buffer);
    request = DefaultRequest();
    assert(SecondaryMcuUpdateService_Init(&service, &dependencies) ==
           FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_Start(&service, &request) ==
           FIRMWARE_STATUS_OK);
    RunToTerminal(&service);
    assert(SecondaryMcuUpdateService_GetState(&service) ==
           SERVICE_RUN_STATE_SUCCEEDED);
    assert(programmer.begin_count == 1U);
    assert(programmer.erase_count == 3U);
    assert(programmer.write_count == 6U);
    assert(programmer.read_count == 6U);
    assert(programmer.end_count == 1U);
    assert(programmer.abort_count == 0U);
    assert(memcmp(programmer.target, source.bytes, IMAGE_SIZE) == 0);
    assert(SecondaryMcuUpdateService_TargetMayBeModified(&service) != 0);
}

static void TestReadbackFailureAborts(void)
{
    source_fixture_t source;
    programmer_fixture_t programmer;
    firmware_image_source_t image = {0};
    mcu_programmer_t target = {0};
    secondary_mcu_update_service_t service = {0};
    uint8_t write_buffer[256];
    uint8_t readback_buffer[256];
    secondary_mcu_update_service_dependencies_t dependencies;
    hash_fixture_t hash_fixture;
    hash_provider_t hash = MakeHash(&hash_fixture);

    FillFixture(&source, &programmer, &image, &target);
    programmer.corrupt_readback = 1;
    dependencies = (secondary_mcu_update_service_dependencies_t) {
        .source = &image,
        .programmer = &target,
        .hash = &hash,
        .write_buffer = write_buffer,
        .readback_buffer = readback_buffer,
        .buffer_size = sizeof(write_buffer),
    };
    assert(SecondaryMcuUpdateService_Init(&service, &dependencies) ==
           FIRMWARE_STATUS_OK);
    {
        secondary_mcu_update_request_t request = DefaultRequest();
        assert(SecondaryMcuUpdateService_Start(&service, &request) ==
               FIRMWARE_STATUS_OK);
    }
    RunToTerminal(&service);
    assert(SecondaryMcuUpdateService_GetState(&service) ==
           SERVICE_RUN_STATE_FAILED);
    assert(SecondaryMcuUpdateService_GetResult(&service)->error ==
           BOOT_ERROR_SECONDARY_MCU_VERIFY);
    assert(programmer.abort_count == 1U);
    assert(programmer.end_count == 0U);
}

static void TestSourceSizeMismatch(void)
{
    source_fixture_t source;
    programmer_fixture_t programmer;
    firmware_image_source_t image = {0};
    mcu_programmer_t target = {0};
    secondary_mcu_update_service_t service = {0};
    uint8_t write_buffer[256];
    uint8_t readback_buffer[256];
    secondary_mcu_update_service_dependencies_t dependencies;
    secondary_mcu_update_request_t request;
    hash_fixture_t hash_fixture;
    hash_provider_t hash = MakeHash(&hash_fixture);

    FillFixture(&source, &programmer, &image, &target);
    dependencies = (secondary_mcu_update_service_dependencies_t) {
        .source = &image,
        .programmer = &target,
        .hash = &hash,
        .write_buffer = write_buffer,
        .readback_buffer = readback_buffer,
        .buffer_size = sizeof(write_buffer),
    };
    request = DefaultRequest();
    request.image_size_bytes = IMAGE_SIZE - 1U;
    assert(SecondaryMcuUpdateService_Init(&service, &dependencies) ==
           FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_Start(&service, &request) ==
           FIRMWARE_STATUS_OK);
    RunToTerminal(&service);
    assert(SecondaryMcuUpdateService_GetState(&service) ==
           SERVICE_RUN_STATE_FAILED);
    assert(SecondaryMcuUpdateService_GetResult(&service)->error ==
           BOOT_ERROR_SECONDARY_MCU_SOURCE);
    assert(programmer.begin_count == 0U);
}

static void TestSourceHashMismatchBeforeErase(void)
{
    source_fixture_t source;
    programmer_fixture_t programmer;
    firmware_image_source_t image = {0};
    mcu_programmer_t target = {0};
    secondary_mcu_update_service_t service = {0};
    uint8_t write_buffer[256];
    uint8_t readback_buffer[256];
    hash_fixture_t hash_fixture;
    hash_provider_t hash = MakeHash(&hash_fixture);
    secondary_mcu_update_service_dependencies_t dependencies = {
        .source = &image,
        .programmer = &target,
        .hash = &hash,
        .write_buffer = write_buffer,
        .readback_buffer = readback_buffer,
        .buffer_size = sizeof(write_buffer),
    };
    secondary_mcu_update_request_t request = DefaultRequest();

    FillFixture(&source, &programmer, &image, &target);
    request.sha256[0] ^= 0x01U;
    assert(SecondaryMcuUpdateService_Init(&service, &dependencies) ==
           FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_Start(&service, &request) ==
           FIRMWARE_STATUS_OK);
    RunToTerminal(&service);
    assert(SecondaryMcuUpdateService_GetState(&service) ==
           SERVICE_RUN_STATE_FAILED);
    assert(SecondaryMcuUpdateService_GetResult(&service)->error ==
           BOOT_ERROR_SECONDARY_MCU_SOURCE);
    assert(programmer.begin_count == 0U);
    assert(programmer.erase_count == 0U);
    assert(SecondaryMcuUpdateService_TargetMayBeModified(&service) == 0);
}

static void TestCancelBeforeBegin(void)
{
    source_fixture_t source;
    programmer_fixture_t programmer;
    firmware_image_source_t image = {0};
    mcu_programmer_t target = {0};
    secondary_mcu_update_service_t service = {0};
    uint8_t write_buffer[256];
    uint8_t readback_buffer[256];
    secondary_mcu_update_service_dependencies_t dependencies;
    secondary_mcu_update_request_t request;
    hash_fixture_t hash_fixture;
    hash_provider_t hash = MakeHash(&hash_fixture);

    FillFixture(&source, &programmer, &image, &target);
    dependencies = (secondary_mcu_update_service_dependencies_t) {
        .source = &image,
        .programmer = &target,
        .hash = &hash,
        .write_buffer = write_buffer,
        .readback_buffer = readback_buffer,
        .buffer_size = sizeof(write_buffer),
    };
    request = DefaultRequest();
    assert(SecondaryMcuUpdateService_Init(&service, &dependencies) ==
           FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_Start(&service, &request) ==
           FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_Cancel(&service) == FIRMWARE_STATUS_OK);
    assert(SecondaryMcuUpdateService_GetState(&service) ==
           SERVICE_RUN_STATE_CANCELLED);
    assert(programmer.begin_count == 0U);
}

int main(void)
{
    TestInstallAndReadback();
    TestReadbackFailureAborts();
    TestSourceSizeMismatch();
    TestSourceHashMismatchBeforeErase();
    TestCancelBeforeBegin();
    return 0;
}
