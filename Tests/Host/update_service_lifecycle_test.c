#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "firmware/hash.h"
#include "services/capability/update_request_service_api.h"
#include "services/common/runtime_layout.h"
#include "services/use_case/update_service.h"

typedef struct
{
    package_file_id_t open_file;
    uint32_t sizes[3];
    int mounted;
    int file_open;
    int open_failure;
    int read_failure;
    int short_read;
    int mutate_after_preflight;
    uint32_t open_count;
    uint32_t close_count;
} source_fixture_t;

typedef struct
{
    uint32_t value;
    uint32_t finish_count;
} hash_fixture_t;

typedef struct
{
    async_block_device_operation_result_t operation;
    uint32_t pending_address;
    uint32_t pending_size;
    uint8_t pending_program[256U];
    uint32_t app_erase_bytes;
    uint32_t gui_erase_bytes;
    uint32_t program_bytes;
    int erase_failure;
    int program_failure;
    int target_read_failure;
    int target_corrupt;
    int source_erased_before_gui_hash;
    int invalid_program_address;
    uint8_t app_runtime[BOOT_APP_RUNTIME_SIZE];
    uint8_t gui_runtime[BOOT_GUI_RUNTIME_SIZE];
} storage_fixture_t;

static source_fixture_t source_fixture;
static hash_fixture_t hash_fixture;
static storage_fixture_t storage_fixture;
static validated_manifest_t manifest_fixture;
static firmware_status_t binding_status;
static manifest_service_t manifest_service;
static update_request_service_t request_service;
static update_service_t update_service;
static package_source_t source_interface;
static hash_provider_t hash_interface;
static async_block_device_t storage_interface;
static uint8_t manifest_buffer[UPDATE_SERVICE_MANIFEST_MAX_SIZE];
static uint8_t io_buffer[UPDATE_SERVICE_IO_BUFFER_MIN_SIZE];

static uint8_t SourceByte(package_file_id_t file, uint32_t offset)
{
    return (uint8_t)((uint32_t)file * 0x31U + offset * 17U + 3U);
}

static void DigestBytes(package_file_id_t file, uint32_t size,
                         uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    hash_fixture_t hash = {2166136261UL, 0U};
    uint32_t offset;

    for (offset = 0U; offset < size; ++offset)
    {
        hash.value ^= SourceByte(file, offset);
        hash.value *= 16777619UL;
    }
    for (offset = 0U; offset < FIRMWARE_SHA256_DIGEST_SIZE; ++offset)
    {
        digest[offset] = (uint8_t)(hash.value >> ((offset % 4U) * 8U));
    }
}

static firmware_status_t SourcePresent(void *context, int *present)
{
    source_fixture_t *source = (source_fixture_t *)context;
    *present = (source->mounted != 0) ? 1 : 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceMount(void *context)
{
    ((source_fixture_t *)context)->mounted = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceUnmount(void *context)
{
    source_fixture_t *source = (source_fixture_t *)context;
    source->mounted = 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceOpen(void *context, package_file_id_t file)
{
    source_fixture_t *source = (source_fixture_t *)context;

    if ((source->mounted == 0) || (source->file_open != 0) || (source->open_failure != 0))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if ((file < PACKAGE_FILE_MANIFEST) || (file > PACKAGE_FILE_GUI))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    source->open_file = file;
    source->file_open = 1;
    ++source->open_count;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceClose(void *context)
{
    source_fixture_t *source = (source_fixture_t *)context;

    source->file_open = 0;
    ++source->close_count;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceSize(void *context, uint32_t *size)
{
    source_fixture_t *source = (source_fixture_t *)context;

    if ((source->file_open == 0) || (size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    *size = source->sizes[source->open_file];
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceRead(void *context, uint32_t offset, uint8_t *data,
                                    uint32_t size, uint32_t *bytes_read)
{
    source_fixture_t *source = (source_fixture_t *)context;
    uint32_t index;

    if ((source->file_open == 0) || (data == NULL) || (bytes_read == NULL) ||
        (offset > source->sizes[source->open_file]) ||
        (size > (source->sizes[source->open_file] - offset)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (source->read_failure != 0)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    *bytes_read = (source->short_read != 0) && (size != 0U) ? size - 1U : size;
    for (index = 0U; index < *bytes_read; ++index)
    {
        if (source->open_file == PACKAGE_FILE_MANIFEST)
        {
            data[index] = manifest_buffer[offset + index];
        }
        else
        {
            data[index] = SourceByte(source->open_file, offset + index);
            if ((source->mutate_after_preflight != 0) && (hash_fixture.finish_count >= 2U) &&
                (offset == 0U) && (index == 0U))
            {
                data[index] ^= 0xA6U;
            }
        }
    }
    return FIRMWARE_STATUS_OK;
}

static package_source_t MakeSource(void)
{
    package_source_t source = {
        .context = &source_fixture,
        .is_media_present = SourcePresent,
        .mount = SourceMount,
        .unmount = SourceUnmount,
        .open = SourceOpen,
        .close = SourceClose,
        .get_size = SourceSize,
        .read_at = SourceRead,
    };

    return source;
}

static firmware_status_t HashResetFixture(void *context)
{
    hash_fixture_t *hash = (hash_fixture_t *)context;
    hash->value = 2166136261UL;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashUpdateFixture(void *context, const void *data, size_t size)
{
    hash_fixture_t *hash = (hash_fixture_t *)context;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0U; index < size; ++index)
    {
        hash->value ^= bytes[index];
        hash->value *= 16777619UL;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashFinishFixture(void *context,
                                           uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    hash_fixture_t *hash = (hash_fixture_t *)context;
    uint32_t index;

    ++hash->finish_count;
    for (index = 0U; index < FIRMWARE_SHA256_DIGEST_SIZE; ++index)
    {
        digest[index] = (uint8_t)(hash->value >> ((index % 4U) * 8U));
    }
    return FIRMWARE_STATUS_OK;
}

static hash_provider_t MakeHash(void)
{
    hash_provider_t hash = {
        .context = &hash_fixture,
        .reset = HashResetFixture,
        .update = HashUpdateFixture,
        .finish = HashFinishFixture,
    };

    return hash;
}

static void StorageMemory(uint32_t address, uint8_t **memory, uint32_t *offset)
{
    if (address < BOOT_GUI_FLASH_OFFSET)
    {
        *memory = storage_fixture.app_runtime;
        *offset = address - BOOT_APP_FLASH_OFFSET;
    }
    else
    {
        *memory = storage_fixture.gui_runtime;
        *offset = address - BOOT_GUI_FLASH_OFFSET;
    }
}

static firmware_status_t StorageGetInfo(void *context, async_block_device_info_t *info)
{
    (void)context;
    info->capacity_bytes = BOOT_GUI_FLASH_OFFSET + BOOT_GUI_RUNTIME_SIZE;
    info->program_size = 256U;
    info->erase_size = 4096U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageRead(void *context, uint32_t address, void *data, uint32_t size)
{
    uint8_t *memory;
    uint32_t offset;

    (void)context;
    if (storage_fixture.target_read_failure != 0)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    StorageMemory(address, &memory, &offset);
    memcpy(data, &memory[offset], size);
    if ((storage_fixture.target_corrupt != 0) && (size != 0U))
    {
        ((uint8_t *)data)[0] ^= 0x5AU;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageProgramStart(void *context, uint32_t address,
                                              const void *data, uint32_t size)
{
    int valid_range;

    (void)context;
    storage_fixture.pending_address = address;
    storage_fixture.pending_size = size;
    valid_range = ((address <= (BOOT_APP_FLASH_OFFSET + BOOT_APP_RUNTIME_SIZE)) &&
                   (size <= (BOOT_APP_FLASH_OFFSET + BOOT_APP_RUNTIME_SIZE - address))) ||
                  ((address >= BOOT_GUI_FLASH_OFFSET) &&
                   (address <= (BOOT_GUI_FLASH_OFFSET + BOOT_GUI_RUNTIME_SIZE)) &&
                   (size <= (BOOT_GUI_FLASH_OFFSET + BOOT_GUI_RUNTIME_SIZE - address)));
    if ((valid_range == 0) || (size > sizeof(storage_fixture.pending_program)))
    {
        storage_fixture.invalid_program_address = 1;
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(storage_fixture.pending_program, data, size);
    if (storage_fixture.program_failure != 0)
    {
        storage_fixture.operation.state = ASYNC_BLOCK_DEVICE_OPERATION_FAILED;
        storage_fixture.operation.status = FIRMWARE_STATUS_IO_ERROR;
    }
    else
    {
        storage_fixture.operation.state = ASYNC_BLOCK_DEVICE_OPERATION_BUSY;
        storage_fixture.operation.status = FIRMWARE_STATUS_OK;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageEraseStart(void *context, uint32_t address, uint32_t size)
{
    (void)context;
    storage_fixture.pending_address = address;
    storage_fixture.pending_size = size;
    if (hash_fixture.finish_count < 2U)
    {
        storage_fixture.source_erased_before_gui_hash = 1;
    }
    if (address < BOOT_GUI_FLASH_OFFSET)
    {
        storage_fixture.app_erase_bytes += size;
    }
    else
    {
        storage_fixture.gui_erase_bytes += size;
    }
    if (storage_fixture.erase_failure != 0)
    {
        storage_fixture.operation.state = ASYNC_BLOCK_DEVICE_OPERATION_FAILED;
        storage_fixture.operation.status = FIRMWARE_STATUS_IO_ERROR;
    }
    else
    {
        storage_fixture.operation.state = ASYNC_BLOCK_DEVICE_OPERATION_BUSY;
        storage_fixture.operation.status = FIRMWARE_STATUS_OK;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StoragePoll(void *context)
{
    uint8_t *memory;
    uint32_t offset;

    (void)context;
    if (storage_fixture.operation.state != ASYNC_BLOCK_DEVICE_OPERATION_BUSY)
    {
        return FIRMWARE_STATUS_OK;
    }
    StorageMemory(storage_fixture.pending_address, &memory, &offset);
    if (storage_fixture.pending_size == 4096U)
    {
        memset(&memory[offset], 0xFF, storage_fixture.pending_size);
    }
    else
    {
        memcpy(&memory[offset], storage_fixture.pending_program, storage_fixture.pending_size);
        storage_fixture.program_bytes += storage_fixture.pending_size;
    }
    storage_fixture.operation.state = ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED;
    storage_fixture.operation.status = FIRMWARE_STATUS_OK;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StorageResult(void *context,
                                       async_block_device_operation_result_t *result)
{
    (void)context;
    *result = storage_fixture.operation;
    return FIRMWARE_STATUS_OK;
}

static async_block_device_t MakeStorage(void)
{
    async_block_device_t storage = {
        .context = &storage_fixture,
        .get_info = StorageGetInfo,
        .read = StorageRead,
        .program_start = StorageProgramStart,
        .erase_start = StorageEraseStart,
        .poll = StoragePoll,
        .get_operation_result = StorageResult,
        .cancel = NULL,
    };

    return storage;
}

firmware_status_t ManifestService_ParseAndValidate(
    struct manifest_service *service, const uint8_t *data, uint32_t size,
    validated_manifest_t *manifest)
{
    (void)service;
    (void)data;
    (void)size;
    *manifest = manifest_fixture;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateRequestService_ValidateManifestBinding(
    struct update_request_service *service, const update_request_t *request,
    const uint8_t *manifest_data, uint32_t manifest_size,
    const validated_manifest_t *manifest)
{
    (void)service;
    (void)request;
    (void)manifest_data;
    (void)manifest_size;
    (void)manifest;
    return binding_status;
}

static void ResetFixture(void)
{
    update_service_dependencies_t dependencies;

    memset(&source_fixture, 0, sizeof(source_fixture));
    memset(&hash_fixture, 0, sizeof(hash_fixture));
    memset(&storage_fixture, 0, offsetof(storage_fixture_t, app_runtime));
    memset(&manifest_fixture, 0, sizeof(manifest_fixture));
    memset(&manifest_service, 0, sizeof(manifest_service));
    memset(&request_service, 0, sizeof(request_service));
    memset(&update_service, 0, sizeof(update_service));
    memset(manifest_buffer, 0xA5, sizeof(manifest_buffer));
    source_fixture.sizes[PACKAGE_FILE_MANIFEST] = 4U;
    source_fixture.sizes[PACKAGE_FILE_APP] = 513U;
    source_fixture.sizes[PACKAGE_FILE_GUI] = 8193U;
    manifest_fixture.app.size_bytes = source_fixture.sizes[PACKAGE_FILE_APP];
    manifest_fixture.gui.size_bytes = source_fixture.sizes[PACKAGE_FILE_GUI];
    manifest_fixture.release_version.major = 2U;
    manifest_fixture.build_number = 42U;
    strcpy(manifest_fixture.package_id, "package-42");
    memset(manifest_fixture.package_id_hash128, 0x11, sizeof(manifest_fixture.package_id_hash128));
    memset(manifest_fixture.manifest_sha256, 0x22, sizeof(manifest_fixture.manifest_sha256));
    DigestBytes(PACKAGE_FILE_APP, manifest_fixture.app.size_bytes, manifest_fixture.app.sha256);
    DigestBytes(PACKAGE_FILE_GUI, manifest_fixture.gui.size_bytes, manifest_fixture.gui.sha256);
    binding_status = FIRMWARE_STATUS_OK;

    source_interface = MakeSource();
    hash_interface = MakeHash();
    storage_interface = MakeStorage();
    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.package_source = &source_interface;
    dependencies.manifest_service = &manifest_service;
    dependencies.update_request_service = &request_service;
    dependencies.hash = &hash_interface;
    dependencies.storage = &storage_interface;
    dependencies.runtime_layout = BootRuntimeLayout_Get();
    dependencies.manifest_buffer = manifest_buffer;
    dependencies.manifest_buffer_size = sizeof(manifest_buffer);
    dependencies.io_buffer = io_buffer;
    dependencies.io_buffer_size = sizeof(io_buffer);
    assert(UpdateService_Init(&update_service, &dependencies) == FIRMWARE_STATUS_OK);
}

static update_request_t MakeRequest(void)
{
    update_request_t request;

    memset(&request, 0, sizeof(request));
    request.format_version = UPDATE_REQUEST_FORMAT_VERSION;
    request.requested = 1U;
    strcpy(request.package_id, "package-42");
    return request;
}

static void RunToTerminal(void)
{
    uint32_t count;

    for (count = 0U; count < 30000U; ++count)
    {
        if (UpdateService_GetState(&update_service) != SERVICE_RUN_STATE_RUNNING)
        {
            return;
        }
        UpdateService_Process(&update_service);
    }
    assert(0 && "fixed runtime installer did not reach terminal state");
}

static void Prepare(void)
{
    update_request_t request = MakeRequest();

    source_fixture.mounted = 1;
    assert(UpdateService_PrepareStart(&update_service, &request) == FIRMWARE_STATUS_OK);
    RunToTerminal();
}

static void Install(void)
{
    assert(UpdateService_InstallStart(&update_service) == FIRMWARE_STATUS_OK);
    RunToTerminal();
}

static void TestSuccessAndCandidate(void)
{
    const boot_active_record_t *candidate;

    ResetFixture();
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    assert(UpdateService_RuntimeMayBeModified(&update_service) == 0);
    assert(storage_fixture.app_erase_bytes == 0U);
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    candidate = UpdateService_GetCandidate(&update_service);
    assert(candidate != NULL);
    assert(candidate->format_version == BOOT_ACTIVE_RECORD_FORMAT_V2);
    assert(candidate->state == BOOT_ACTIVE_RECORD_STATE_VALID);
    assert(candidate->sequence == 0U);
    assert(candidate->release_version.major == manifest_fixture.release_version.major);
    assert(candidate->build_number == manifest_fixture.build_number);
    assert(candidate->app_size == manifest_fixture.app.size_bytes);
    assert(candidate->gui_size == manifest_fixture.gui.size_bytes);
    assert(memcmp(candidate->package_id_hash, manifest_fixture.package_id_hash128,
                  sizeof(candidate->package_id_hash)) == 0);
    assert(memcmp(candidate->manifest_sha256, manifest_fixture.manifest_sha256,
                  sizeof(candidate->manifest_sha256)) == 0);
    assert(memcmp(candidate->app_sha256, manifest_fixture.app.sha256,
                  sizeof(candidate->app_sha256)) == 0);
    assert(memcmp(candidate->gui_sha256, manifest_fixture.gui.sha256,
                  sizeof(candidate->gui_sha256)) == 0);
    assert(storage_fixture.app_erase_bytes == BOOT_APP_RUNTIME_SIZE);
    assert(storage_fixture.gui_erase_bytes == BOOT_GUI_RUNTIME_SIZE);
    assert(storage_fixture.program_bytes == manifest_fixture.app.size_bytes +
                                              manifest_fixture.gui.size_bytes);
    assert(storage_fixture.source_erased_before_gui_hash == 0);
    assert(storage_fixture.invalid_program_address == 0);
    assert(UpdateService_RuntimeMayBeModified(&update_service) != 0);
}

static void TestStateGuardsAndLegacy(void)
{
    update_request_t request;

    ResetFixture();
    request = MakeRequest();
    assert(UpdateService_InstallStart(&update_service) == FIRMWARE_STATUS_INVALID_STATE);
    assert(UpdateService_PrepareStart(&update_service, &request) == FIRMWARE_STATUS_OK);
    assert(UpdateService_PrepareStart(&update_service, &request) == FIRMWARE_STATUS_INVALID_STATE);
    assert(UpdateService_InstallStart(&update_service) == FIRMWARE_STATUS_INVALID_STATE);
    ResetFixture();
    Prepare();
    assert(UpdateService_InstallStart(&update_service) == FIRMWARE_STATUS_OK);
    assert(UpdateService_PrepareStart(&update_service, &request) == FIRMWARE_STATUS_INVALID_STATE);
}

static void TestSourceFailuresDoNotErase(void)
{
    ResetFixture();
    binding_status = FIRMWARE_STATUS_INVALID_STATE;
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);

    ResetFixture();
    source_fixture.mounted = 1;
    source_fixture.sizes[PACKAGE_FILE_APP] = 514U;
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);
    assert(UpdateService_RuntimeMayBeModified(&update_service) == 0);

    ResetFixture();
    source_fixture.mounted = 1;
    manifest_fixture.gui.sha256[0] ^= 1U;
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);
}

static void TestTargetFailures(void)
{
    ResetFixture();
    source_fixture.mutate_after_preflight = 1;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == BOOT_APP_RUNTIME_SIZE);
    assert(storage_fixture.gui_erase_bytes == 0U);

    ResetFixture();
    storage_fixture.erase_failure = 1;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.gui_erase_bytes == 0U);
    assert(UpdateService_GetCandidate(&update_service) == NULL);
    assert(UpdateService_RuntimeMayBeModified(&update_service) != 0);

    ResetFixture();
    storage_fixture.program_failure = 1;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.gui_erase_bytes == 0U);

    ResetFixture();
    storage_fixture.target_read_failure = 1;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.gui_erase_bytes == 0U);

    ResetFixture();
    storage_fixture.target_corrupt = 1;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.gui_erase_bytes == 0U);
}

static void TestRuntimeBounds(void)
{
    ResetFixture();
    source_fixture.sizes[PACKAGE_FILE_APP] = BOOT_APP_RUNTIME_SIZE + 1U;
    manifest_fixture.app.size_bytes = source_fixture.sizes[PACKAGE_FILE_APP];
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);

    ResetFixture();
    source_fixture.sizes[PACKAGE_FILE_GUI] = BOOT_GUI_RUNTIME_SIZE + 1U;
    manifest_fixture.gui.size_bytes = source_fixture.sizes[PACKAGE_FILE_GUI];
    Prepare();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_SUCCEEDED);
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);

    ResetFixture();
    source_fixture.sizes[PACKAGE_FILE_APP] = BOOT_APP_RUNTIME_SIZE;
    manifest_fixture.app.size_bytes = BOOT_APP_RUNTIME_SIZE;
    DigestBytes(PACKAGE_FILE_APP, manifest_fixture.app.size_bytes, manifest_fixture.app.sha256);
    source_fixture.sizes[PACKAGE_FILE_GUI] = 1U;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);

    ResetFixture();
    source_fixture.sizes[PACKAGE_FILE_GUI] = BOOT_GUI_RUNTIME_SIZE;
    manifest_fixture.gui.size_bytes = BOOT_GUI_RUNTIME_SIZE;
    DigestBytes(PACKAGE_FILE_GUI, manifest_fixture.gui.size_bytes, manifest_fixture.gui.sha256);
    manifest_fixture.gui.sha256[0] ^= 1U;
    Prepare();
    Install();
    assert(UpdateService_GetState(&update_service) == SERVICE_RUN_STATE_FAILED);
    assert(storage_fixture.app_erase_bytes == 0U);
}

int main(void)
{
    TestSuccessAndCandidate();
    TestStateGuardsAndLegacy();
    TestSourceFailuresDoNotErase();
    TestTargetFailures();
    TestRuntimeBounds();
    return 0;
}
