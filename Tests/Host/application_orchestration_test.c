#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "application/application.h"
#include "application/application_config.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/update_request_service_api.h"
#include "services/use_case/active_validation_service_api.h"
#include "services/use_case/launch_service_api.h"
#include "services/use_case/update_service_api.h"

struct boot_control_service { int unused; };
struct update_service { int unused; };
struct active_validation_service { int unused; };
struct launch_service { int unused; };
struct update_request_service { int unused; };

static struct boot_control_service boot_control;
static struct update_service update;
static struct active_validation_service validation;
static struct launch_service launch;
static struct update_request_service request_parser;
static boot_active_record_t active_record;
static boot_active_record_t candidate_record;
static validated_manifest_t manifest;
static service_result_t update_result;
static service_run_state_t update_state;
static service_run_state_t commit_state;
static service_run_state_t validation_state;
static int update_phase;
static int runtime_modified;
static int media_present = 1;
static int source_mounted;
static int request_present = 1;
static int clear_status = FIRMWARE_STATUS_OK;
static int mount_status = FIRMWARE_STATUS_OK;
static int unmount_status = FIRMWARE_STATUS_OK;
static int commit_status = FIRMWARE_STATUS_OK;
static int install_status = FIRMWARE_STATUS_OK;
static int validation_success = 1;
static uint32_t prepare_count;
static uint32_t install_count;
static uint32_t commit_count;
static uint32_t clear_count;
static uint32_t mount_count;
static uint32_t unmount_count;
static uint32_t unmount_failures_remaining;
static uint32_t validation_count;
static uint32_t launch_count;
static uint32_t reset_count;
static uint32_t recovery_count;
static jmp_buf reset_jump;

static firmware_status_t SourcePresent(void *context, int *present)
{
    (void)context;
    *present = media_present;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceMount(void *context)
{
    firmware_status_t status;

    (void)context;
    ++mount_count;
    status = (firmware_status_t)mount_status;
    if (FirmwareStatus_IsOk(status))
    {
        source_mounted = 1;
    }
    return status;
}

static firmware_status_t SourceUnmount(void *context)
{
    firmware_status_t status;

    (void)context;
    ++unmount_count;
    if (source_mounted == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (unmount_failures_remaining != 0U)
    {
        --unmount_failures_remaining;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    status = (firmware_status_t)unmount_status;
    if (FirmwareStatus_IsOk(status))
    {
        source_mounted = 0;
    }
    return status;
}

static firmware_status_t SourceOpen(void *context, package_file_id_t file)
{
    (void)context;
    (void)file;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceClose(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceSize(void *context, uint32_t *size)
{
    (void)context;
    *size = 1U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceRead(void *context, uint32_t offset, uint8_t *data,
                                    uint32_t size, uint32_t *bytes_read)
{
    (void)context;
    (void)offset;
    memset(data, 0, size);
    *bytes_read = size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequestLoad(void *context, uint8_t *buffer, uint32_t capacity,
                                      uint32_t *size)
{
    (void)context;
    if (request_present == 0)
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    assert(capacity >= 2U);
#if defined(TEST_INVALID_REQUEST)
    buffer[0] = '!';
#else
    buffer[0] = 1U;
#endif
    buffer[1] = 0U;
    *size = 2U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequestClear(void *context)
{
    (void)context;
    ++clear_count;
    return (firmware_status_t)clear_status;
}

firmware_status_t UpdateRequestService_ParseAndValidate(
    struct update_request_service *service, const uint8_t *data, uint32_t size,
    update_request_t *request)
{
    (void)service;
    if ((data == NULL) || (size == 0U) || (data[0] == '!'))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    memset(request, 0, sizeof(*request));
    request->format_version = UPDATE_REQUEST_FORMAT_VERSION;
    request->requested = 1U;
    (void)strcpy(request->package_id, "test-package");
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_LoadActive(
    struct boot_control_service *service, boot_active_record_t *record)
{
    (void)service;
#if defined(TEST_INITIAL_INSTALL) && !defined(TEST_MEDIA_REMOVAL)
    (void)record;
    return FIRMWARE_STATUS_INVALID_STATE;
#else
    *record = active_record;
    return FIRMWARE_STATUS_OK;
#endif
}

firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service, const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++commit_count;
    if (commit_status != FIRMWARE_STATUS_OK)
    {
        return (firmware_status_t)commit_status;
    }
    commit_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void BootControlService_Process(struct boot_control_service *service)
{
    (void)service;
    commit_state = SERVICE_RUN_STATE_SUCCEEDED;
}

service_run_state_t BootControlService_GetState(const struct boot_control_service *service)
{
    (void)service;
    return commit_state;
}

const service_result_t *BootControlService_GetResult(const struct boot_control_service *service)
{
    (void)service;
    return NULL;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service,
                                             const update_request_t *request)
{
    (void)service;
    (void)request;
    ++prepare_count;
    update_phase = 1;
    update_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStart(struct update_service *service)
{
    (void)service;
    ++install_count;
    if (install_status != FIRMWARE_STATUS_OK)
    {
        update_state = SERVICE_RUN_STATE_FAILED;
        return (firmware_status_t)install_status;
    }
    update_phase = 2;
    runtime_modified = 0;
    update_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void UpdateService_Process(struct update_service *service)
{
    (void)service;
    if (update_phase == 1)
    {
        update_state = SERVICE_RUN_STATE_SUCCEEDED;
#if defined(TEST_VERSION_REJECT)
        manifest.release_version.major = 1U;
#else
        manifest.release_version.major = 2U;
#endif
    }
    else if (update_phase == 2)
    {
#if defined(TEST_INSTALL_POST_MUTATION)
        runtime_modified = 1;
        update_state = SERVICE_RUN_STATE_FAILED;
#elif defined(TEST_INSTALL_XIP_FAILURE)
        update_result.status = FIRMWARE_STATUS_IO_ERROR;
        update_result.error = BOOT_ERROR_XIP_SETUP;
        update_state = SERVICE_RUN_STATE_FAILED;
#else
        update_state = SERVICE_RUN_STATE_SUCCEEDED;
#endif
    }
}

firmware_status_t UpdateService_Cancel(struct update_service *service)
{
    (void)service;
    return FIRMWARE_STATUS_OK;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    (void)service;
    return update_state;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    (void)service;
    return &update_result;
}

const validated_manifest_t *UpdateService_GetManifest(const struct update_service *service)
{
    (void)service;
    return (update_phase == 1 || update_phase == 2) ? &manifest : NULL;
}

const boot_active_record_t *UpdateService_GetCandidate(const struct update_service *service)
{
    (void)service;
    candidate_record.release_version.major = 2U;
    candidate_record.app_size = 1U;
    candidate_record.gui_size = 1U;
    candidate_record.format_version = BOOT_ACTIVE_RECORD_FORMAT_V2;
    return &candidate_record;
}

int UpdateService_RuntimeMayBeModified(const struct update_service *service)
{
    (void)service;
    return runtime_modified;
}

firmware_status_t ActiveValidationService_Start(struct active_validation_service *service,
                                                const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++validation_count;
    validation_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void ActiveValidationService_Process(struct active_validation_service *service)
{
    (void)service;
    validation_state = validation_success ? SERVICE_RUN_STATE_SUCCEEDED
                                           : SERVICE_RUN_STATE_FAILED;
}

service_run_state_t ActiveValidationService_GetState(
    const struct active_validation_service *service)
{
    (void)service;
    return validation_state;
}

const service_result_t *ActiveValidationService_GetResult(
    const struct active_validation_service *service)
{
    (void)service;
    return NULL;
}

firmware_status_t LaunchService_Execute(struct launch_service *service,
                                       const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++launch_count;
    return FIRMWARE_STATUS_OK;
}

const service_result_t *LaunchService_GetResult(const struct launch_service *service)
{
    (void)service;
    return NULL;
}

static void Reset(void *context)
{
    (void)context;
    ++reset_count;
    longjmp(reset_jump, 1);
}

int main(void)
{
    package_source_t source = {0};
    update_request_store_t store = {0};
    system_reset_t reset = {0};
    application_dependencies_t dependencies = {0};
    uint32_t step;

    (void)active_record;
    (void)recovery_count;

    source.is_media_present = SourcePresent;
    source.mount = SourceMount;
    source.unmount = SourceUnmount;
    source.open = SourceOpen;
    source.close = SourceClose;
    source.get_size = SourceSize;
    source.read_at = SourceRead;
    store.load_raw = RequestLoad;
    store.clear = RequestClear;
    reset.request = Reset;
    manifest.minimum_bootloader_version.major = 1U;
    manifest.release_version.major = 2U;
#if !defined(TEST_INITIAL_INSTALL) || defined(TEST_MEDIA_REMOVAL)
    active_record.release_version.major = 1U;
    active_record.app_size = 1U;
    active_record.gui_size = 1U;
    active_record.package_id_hash[0] = 0xA5U;
#endif
#if defined(TEST_STALE_VALID) || defined(TEST_STALE_INVALID)
    memset(active_record.package_id_hash, 0, sizeof(active_record.package_id_hash));
    memset(active_record.manifest_sha256, 0, sizeof(active_record.manifest_sha256));
#endif

#if defined(TEST_MEDIA_REMOVAL)
    request_present = 0;
#endif
#if defined(TEST_MEDIA_MOUNT_FAILURE)
    mount_status = FIRMWARE_STATUS_IO_ERROR;
#endif
#if defined(TEST_UNMOUNT_RETRY)
    unmount_failures_remaining = 2U;
#endif
#if defined(TEST_UNMOUNT_PERMANENT_FAILURE)
    unmount_failures_remaining = 100U;
#endif
#if defined(TEST_RUNTIME_INVALID_NO_REQUEST)
    request_present = 0;
    validation_success = 0;
#endif
#if defined(TEST_STALE_INVALID)
    validation_success = 0;
#endif
#if defined(TEST_INSTALL_PRE_MUTATION)
    install_status = FIRMWARE_STATUS_IO_ERROR;
#endif
#if defined(TEST_COMMIT_FAILURE)
    commit_status = FIRMWARE_STATUS_IO_ERROR;
#endif
#if defined(TEST_CLEAR_FAILURE)
    clear_status = FIRMWARE_STATUS_IO_ERROR;
#endif

    dependencies.boot_control = &boot_control;
    dependencies.update = &update;
    dependencies.validation = &validation;
    dependencies.launch = &launch;
    dependencies.package_source = &source;
    dependencies.update_request_store = &store;
    dependencies.update_request_service = &request_parser;
    dependencies.system_reset = &reset;
    dependencies.bootloader_version.major = 1U;

    assert(Application_Configure(&dependencies) == FIRMWARE_STATUS_OK);
    assert(Application_Init() == FIRMWARE_STATUS_OK);
    if (setjmp(reset_jump) == 0)
    {
        for (step = 0U; step < 64U; ++step)
        {
            if (Application_Process() != FIRMWARE_STATUS_OK)
            {
                break;
            }
            if (launch_count != 0U)
            {
                break;
            }
#if defined(TEST_MEDIA_MOUNT_FAILURE)
            if (step > 4U)
            {
                break;
            }
#endif
        }
    }

#if defined(TEST_INVALID_REQUEST)
    assert(prepare_count == 0U);
#elif defined(TEST_INITIAL_INSTALL)
    assert(prepare_count == (request_present != 0 ? 1U : 0U));
#else
    assert(prepare_count == (request_present != 0 ? 1U : 0U));
#endif
#if defined(TEST_MEDIA_MOUNT_FAILURE)
    assert(install_count == 0U);
    assert(commit_count == 0U);
    assert(launch_count == 1U);
#elif defined(TEST_MEDIA_REMOVAL)
    assert(install_count == 0U);
    assert(commit_count == 0U);
    assert(launch_count == 1U);
#elif defined(TEST_INVALID_REQUEST) || defined(TEST_VERSION_REJECT)
    assert(install_count == 0U);
    assert(commit_count == 0U);
    assert(clear_count == 0U);
    assert(launch_count == 1U);
#elif defined(TEST_STALE_VALID)
    assert(install_count == 0U);
    assert(commit_count == 0U);
    assert(clear_count == 1U);
    assert(launch_count == 1U);
#elif defined(TEST_STALE_INVALID)
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(reset_count == 1U);
#elif defined(TEST_INSTALL_PRE_MUTATION)
    assert(install_count == 1U);
    assert(commit_count == 0U);
    assert(clear_count == 0U);
    assert(launch_count == 1U);
#elif defined(TEST_COMMIT_FAILURE)
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(clear_count == 0U);
    assert(launch_count == 0U);
#elif defined(TEST_CLEAR_FAILURE)
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(clear_count == 1U);
    assert(reset_count == 1U);
#elif defined(TEST_RUNTIME_INVALID_NO_REQUEST)
    assert(install_count == 0U);
    assert(commit_count == 0U);
    assert(launch_count == 0U);
#elif defined(TEST_INSTALL_POST_MUTATION)
    assert(install_count == 1U);
    assert(commit_count == 0U);
    assert(launch_count == 0U);
    assert(unmount_count == 1U);
    assert(source_mounted == 0);
    assert(reset_count == 1U);
#elif defined(TEST_INSTALL_XIP_FAILURE)
    assert(install_count == 1U);
    assert(commit_count == 0U);
    assert(clear_count == 0U);
    assert(unmount_count == 1U);
    assert(source_mounted == 0);
    assert(reset_count == 1U);
    assert(launch_count == 0U);
#elif defined(TEST_UNMOUNT_RETRY)
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(unmount_count == 3U);
    assert(source_mounted == 0);
    assert(reset_count == 1U);
    assert(launch_count == 0U);
#elif defined(TEST_UNMOUNT_PERMANENT_FAILURE)
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(unmount_count == 3U);
    assert(source_mounted != 0);
    assert(reset_count == 0U);
    assert(launch_count == 0U);
#else
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(clear_count == 1U);
    assert(reset_count == 1U);
    assert(recovery_count == 0U);
#endif
    return 0;
}
