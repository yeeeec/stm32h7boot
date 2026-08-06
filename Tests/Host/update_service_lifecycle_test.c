#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "services/capability/slot_policy.h"
#include "services/use_case/update_service.h"

typedef struct
{
    uint32_t manifest_size;
    int short_read;
    int mounted;
    int file_open;
    int remove_fail;
} source_context_t;

static source_context_t source_context;
static async_block_device_operation_result_t storage_operation;
static validated_manifest_t parsed_manifest;
static firmware_status_t manifest_status;
static uint32_t erase_count;
static uint32_t program_count;
static uint32_t request_remove_count;

static update_service_t update_service;
static manifest_service_t manifest_service;
static uint8_t manifest_buffer[UPDATE_SERVICE_MANIFEST_MAX_SIZE];
static uint8_t io_buffer[UPDATE_SERVICE_IO_BUFFER_MIN_SIZE];
static uint8_t relocation_buffer[
    UPDATE_SERVICE_MAX_RELOCATIONS * APPX_RELOCATION_ENTRY_SIZE];
static appx_relocation_entry_t relocation_entries[
    UPDATE_SERVICE_MAX_RELOCATIONS];

static firmware_status_t SourceIsPresent(void *context, int *present)
{
    (void)context;
    *present = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceMount(void *context)
{
    ((source_context_t *)context)->mounted = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceUnmount(void *context)
{
    ((source_context_t *)context)->mounted = 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceOpen(void *context, const char *path)
{
    (void)path;
    ((source_context_t *)context)->file_open = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceClose(void *context)
{
    ((source_context_t *)context)->file_open = 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceExists(void *context, const char *path, int *present)
{
    (void)context;
    (void)path;
    *present = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceRemove(void *context, const char *path)
{
    (void)context;
    (void)path;
    ++request_remove_count;
    return (((source_context_t *)context)->remove_fail != 0)
               ? FIRMWARE_STATUS_IO_ERROR
               : FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceGetSize(void *context, uint32_t *size)
{
    *size = ((const source_context_t *)context)->manifest_size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceReadAt(
    void *context,
    uint32_t offset,
    void *data,
    uint32_t size,
    uint32_t *bytes_read)
{
    source_context_t *source = (source_context_t *)context;

    (void)offset;
    memset(data, 0, size);
    *bytes_read = ((source->short_read != 0) && (size != 0U))
                      ? size - 1U
                      : size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageGetInfo(
    void *context,
    async_block_device_info_t *info)
{
    (void)context;
    info->capacity_bytes = SLOT_POLICY_FLASH_CAPACITY_BYTES;
    info->program_size = 256U;
    info->erase_size = SLOT_POLICY_ERASE_SIZE_BYTES;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageRead(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    (void)context;
    (void)address;
    memset(data, 0xFF, size);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageProgramStart(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    (void)context;
    (void)address;
    (void)data;
    (void)size;
    ++program_count;
    storage_operation.state = ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED;
    storage_operation.status = FIRMWARE_STATUS_OK;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageEraseStart(
    void *context,
    uint32_t address,
    uint32_t size)
{
    (void)context;
    (void)address;
    (void)size;
    ++erase_count;
    storage_operation.state = ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED;
    storage_operation.status = FIRMWARE_STATUS_OK;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StoragePoll(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageGetOperationResult(
    void *context,
    async_block_device_operation_result_t *result)
{
    (void)context;
    *result = storage_operation;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ChecksumReset(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ChecksumUpdate(
    void *context,
    const void *data,
    size_t size)
{
    (void)context;
    (void)data;
    (void)size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ChecksumGetValue(void *context, uint32_t *value)
{
    (void)context;
    *value = 0U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashReset(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashUpdate(
    void *context,
    const void *data,
    size_t size)
{
    (void)context;
    (void)data;
    (void)size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashFinish(
    void *context,
    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    (void)context;
    memset(digest, 0, FIRMWARE_SHA256_DIGEST_SIZE);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ManifestService_ParseAndValidate(
    struct manifest_service *service,
    const uint8_t *data,
    uint32_t size,
    validated_manifest_t *manifest)
{
    (void)service;
    (void)data;
    (void)size;
    if (FirmwareStatus_IsOk(manifest_status))
    {
        *manifest = parsed_manifest;
    }
    return manifest_status;
}

static package_source_t MakePackageSource(void)
{
    package_source_t source = {
        .context = &source_context,
        .is_media_present = SourceIsPresent,
        .mount = SourceMount,
        .unmount = SourceUnmount,
        .open = SourceOpen,
        .close = SourceClose,
        .exists = SourceExists,
        .remove = SourceRemove,
        .get_size = SourceGetSize,
        .read_at = SourceReadAt,
    };

    return source;
}

static async_block_device_t MakeStorage(void)
{
    async_block_device_t storage = {
        .context = NULL,
        .get_info = StorageGetInfo,
        .read = StorageRead,
        .program_start = StorageProgramStart,
        .erase_start = StorageEraseStart,
        .poll = StoragePoll,
        .get_operation_result = StorageGetOperationResult,
        .cancel = NULL,
    };

    return storage;
}

static checksum_t MakeChecksum(void)
{
    checksum_t checksum = {
        .context = NULL,
        .reset = ChecksumReset,
        .update = ChecksumUpdate,
        .get_value = ChecksumGetValue,
    };

    return checksum;
}

static hash_provider_t MakeHash(void)
{
    hash_provider_t hash = {
        .context = NULL,
        .reset = HashReset,
        .update = HashUpdate,
        .finish = HashFinish,
    };

    return hash;
}

static void ResetFixture(void)
{
    memset(&source_context, 0, sizeof(source_context));
    memset(&storage_operation, 0, sizeof(storage_operation));
    memset(&parsed_manifest, 0, sizeof(parsed_manifest));
    memset(&update_service, 0, sizeof(update_service));
    memset(&manifest_service, 0, sizeof(manifest_service));
    source_context.manifest_size = 4U;
    manifest_status = FIRMWARE_STATUS_OK;
    erase_count = 0U;
    program_count = 0U;
    request_remove_count = 0U;
    parsed_manifest.minimum_bootloader_version.major = 1U;
    parsed_manifest.release_version.major = 2U;
    memset(
        parsed_manifest.package_id_hash128,
        0x5AU,
        sizeof(parsed_manifest.package_id_hash128));
}

static void Initialize(update_service_t *service)
{
    static package_source_t source;
    static async_block_device_t storage;
    static checksum_t checksum;
    static hash_provider_t hash;
    update_service_dependencies_t dependencies;

    source = MakePackageSource();
    storage = MakeStorage();
    checksum = MakeChecksum();
    hash = MakeHash();
    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.package_source = &source;
    dependencies.storage = &storage;
    dependencies.checksum = &checksum;
    dependencies.hash = &hash;
    dependencies.manifest_service = &manifest_service;
    dependencies.manifest_path = "manifest.json";
    dependencies.app_path = "hmi.app.bin";
    dependencies.gui_path = "hmi.gui.bin";
    dependencies.manifest_buffer = manifest_buffer;
    dependencies.manifest_buffer_size = sizeof(manifest_buffer);
    dependencies.io_buffer = io_buffer;
    dependencies.io_buffer_size = sizeof(io_buffer);
    dependencies.relocation_buffer = relocation_buffer;
    dependencies.relocation_buffer_size = sizeof(relocation_buffer);
    dependencies.relocation_entries = relocation_entries;
    dependencies.relocation_entry_capacity =
        UPDATE_SERVICE_MAX_RELOCATIONS;
    assert(UpdateService_Init(service, &dependencies) == FIRMWARE_STATUS_OK);
}

static void RunToTerminal(update_service_t *service)
{
    uint32_t steps;

    for (steps = 0U; steps < 64U; ++steps)
    {
        if (UpdateService_GetState(service) != SERVICE_RUN_STATE_RUNNING)
        {
            return;
        }
        UpdateService_Process(service);
    }
    assert(0 && "Update Service did not reach a terminal state");
}

static void TestCancelBeforeErase(void)
{
    ResetFixture();
    Initialize(&update_service);
    assert(UpdateService_PrepareStart(&update_service) == FIRMWARE_STATUS_OK);
    assert(UpdateService_Cancel(&update_service) == FIRMWARE_STATUS_OK);
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_CANCELLED);
    assert(erase_count == 0U);
    assert(program_count == 0U);
}

static void TestManifestShortReadDoesNotErase(void)
{
    const service_result_t *result;

    ResetFixture();
    source_context.short_read = 1;
    Initialize(&update_service);
    assert(UpdateService_PrepareStart(&update_service) == FIRMWARE_STATUS_OK);
    RunToTerminal(&update_service);
    result = UpdateService_GetResult(&update_service);
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(result != NULL);
    assert(result->error == BOOT_ERROR_MANIFEST_FORMAT);
    assert(erase_count == 0U);
    assert(program_count == 0U);
    assert(request_remove_count == 0U);
}

static void TestPrepareOnlyReturnsManifest(void)
{
    const validated_manifest_t *manifest;

    ResetFixture();
    Initialize(&update_service);
    assert(UpdateService_PrepareStart(&update_service) == FIRMWARE_STATUS_OK);
    RunToTerminal(&update_service);
    manifest = UpdateService_GetManifest(&update_service);
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    assert(manifest != NULL);
    assert(UpdateService_GetCandidate(&update_service) == NULL);
    assert(erase_count == 0U);
    assert(program_count == 0U);
    assert(request_remove_count == 0U);
    assert(source_context.file_open == 0);
}

static void TestInitialInstallSelectsExplicitPair(void)
{
    ResetFixture();
    Initialize(&update_service);
    assert(UpdateService_PrepareStart(&update_service) == FIRMWARE_STATUS_OK);
    RunToTerminal(&update_service);
    assert(UpdateService_InitialInstallStart(
               &update_service, BOOT_PAIR_NONE) == FIRMWARE_STATUS_OUT_OF_RANGE);
    assert(UpdateService_InitialInstallStart(
               &update_service, BOOT_PAIR_1) == FIRMWARE_STATUS_OK);
    assert(update_service.initial_install != 0);
    assert(update_service.initial_target_pair == BOOT_PAIR_1);
    UpdateService_Process(&update_service);
    assert(update_service.target_layout.pair == BOOT_PAIR_1);
    assert(update_service.stage == UPDATE_STAGE_OPEN_APP);
    assert(erase_count == 0U);
}

int main(void)
{
    TestCancelBeforeErase();
    TestManifestShortReadDoesNotErase();
    TestPrepareOnlyReturnsManifest();
    TestInitialInstallSelectsExplicitPair();
    return 0;
}
