#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "application/application.h"
#include "application/application_config.h"
#include "services/capability/boot_control_service_api.h"
#include "services/use_case/active_validation_service_api.h"
#include "services/use_case/launch_service_api.h"
#include "services/use_case/recovery_service_api.h"
#include "services/use_case/update_service_api.h"

struct boot_control_service { int unused; };
struct update_service { int unused; };
struct recovery_service { int unused; };
struct active_validation_service { int unused; };
struct launch_service { int unused; };

static struct boot_control_service boot_control;
static struct update_service update;
static struct recovery_service recovery;
static struct active_validation_service validation;
static struct launch_service launch;
static boot_active_record_t active_record;
static boot_active_record_t candidate_record;
static validated_manifest_t manifest;
static service_run_state_t update_state;
static service_run_state_t commit_state;
static service_run_state_t validation_state;
static uint32_t install_count;
static uint32_t commit_count;
static uint32_t recovery_commit_count;
static uint32_t remove_count;
static uint32_t unmount_count;
static uint32_t reset_count;
static jmp_buf reset_jump;

firmware_status_t RecoveryService_Start(
    struct recovery_service *service, boot_pair_t preferred_pair)
{
    (void)service;
    (void)preferred_pair;
    return FIRMWARE_STATUS_INVALID_STATE;
}

void RecoveryService_Process(struct recovery_service *service)
{
    (void)service;
}

service_run_state_t RecoveryService_GetState(
    const struct recovery_service *service)
{
    (void)service;
    return SERVICE_RUN_STATE_FAILED;
}

const service_result_t *RecoveryService_GetResult(
    const struct recovery_service *service)
{
    (void)service;
    return NULL;
}

const boot_active_record_t *RecoveryService_GetCandidate(
    const struct recovery_service *service)
{
    (void)service;
    return NULL;
}

static firmware_status_t MediaPresent(void *context, int *present)
{
    (void)context;
    *present = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Mount(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Unmount(void *context)
{
    (void)context;
    ++unmount_count;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Exists(void *context, const char *path, int *present)
{
    (void)context;
    assert(strcmp(path, "/boot_update_request.json") == 0);
    *present = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Remove(void *context, const char *path)
{
    (void)context;
    assert(strcmp(path, "/boot_update_request.json") == 0);
    ++remove_count;
    return FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t BootControlService_LoadActive(
    struct boot_control_service *service, boot_active_record_t *record)
{
    (void)service;
    *record = active_record;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service, const boot_active_record_t *record)
{
    (void)service;
    assert(record->active_pair == BOOT_PAIR_2);
    ++commit_count;
    commit_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_CommitRecoveredStart(
    struct boot_control_service *service, const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++recovery_commit_count;
    return FIRMWARE_STATUS_INVALID_STATE;
}

void BootControlService_Process(struct boot_control_service *service)
{
    (void)service;
    commit_state = SERVICE_RUN_STATE_SUCCEEDED;
}

service_run_state_t BootControlService_GetState(
    const struct boot_control_service *service)
{
    (void)service;
    return commit_state;
}

const service_result_t *BootControlService_GetResult(
    const struct boot_control_service *service)
{
    (void)service;
    return NULL;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service)
{
    (void)service;
    update_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStart(
    struct update_service *service, const boot_active_record_t *record)
{
    (void)service;
    assert(record->active_pair == BOOT_PAIR_1);
    ++install_count;
    update_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void UpdateService_Process(struct update_service *service)
{
    (void)service;
    update_state = SERVICE_RUN_STATE_SUCCEEDED;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    (void)service;
    return update_state;
}

const validated_manifest_t *UpdateService_GetManifest(
    const struct update_service *service)
{
    (void)service;
    return &manifest;
}

const boot_active_record_t *UpdateService_GetCandidate(
    const struct update_service *service)
{
    (void)service;
    return (install_count == 0U) ? NULL : &candidate_record;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    (void)service;
    return NULL;
}

firmware_status_t UpdateService_Cancel(struct update_service *service)
{
    (void)service;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ActiveValidationService_Start(
    struct active_validation_service *service,
    const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    validation_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void ActiveValidationService_Process(struct active_validation_service *service)
{
    (void)service;
    validation_state = SERVICE_RUN_STATE_SUCCEEDED;
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

firmware_status_t LaunchService_Execute(
    struct launch_service *service, const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    return FIRMWARE_STATUS_INVALID_STATE;
}

const service_result_t *LaunchService_GetResult(
    const struct launch_service *service)
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
    package_source_t source;
    system_reset_t system_reset;
    application_dependencies_t dependencies;
    uint32_t step;

    memset(&source, 0, sizeof(source));
    memset(&dependencies, 0, sizeof(dependencies));
    memset(&system_reset, 0, sizeof(system_reset));
    memset(&active_record, 0, sizeof(active_record));
    memset(&candidate_record, 0, sizeof(candidate_record));
    memset(&manifest, 0, sizeof(manifest));
    active_record.active_pair = BOOT_PAIR_1;
    active_record.app_size = 1U;
    active_record.gui_size = 1U;
    active_record.release_version.major = 1U;
    candidate_record = active_record;
    candidate_record.active_pair = BOOT_PAIR_2;
    candidate_record.release_version.major = 2U;
    manifest.release_version.major = 2U;
    manifest.minimum_bootloader_version.major = 1U;

    source.is_media_present = MediaPresent;
    source.mount = Mount;
    source.unmount = Unmount;
    source.exists = Exists;
    source.remove = Remove;
    system_reset.request = Reset;
    dependencies.boot_control = &boot_control;
    dependencies.update = &update;
    dependencies.recovery = &recovery;
    dependencies.validation = &validation;
    dependencies.launch = &launch;
    dependencies.package_source = &source;
    dependencies.system_reset = &system_reset;
    dependencies.bootloader_version.major = 1U;
    dependencies.request_path = "/boot_update_request.json";

    assert(Application_Configure(&dependencies) == FIRMWARE_STATUS_OK);
    assert(Application_Init() == FIRMWARE_STATUS_OK);
    if (setjmp(reset_jump) == 0)
    {
        for (step = 0U; step < 32U; ++step)
        {
            assert(Application_Process() == FIRMWARE_STATUS_OK);
        }
        assert(0 && "Application did not reset after committed update");
    }
    assert(install_count == 1U);
    assert(commit_count == 1U);
    assert(recovery_commit_count == 0U);
    assert(remove_count == 1U);
    assert(unmount_count == 1U);
    assert(reset_count == 1U);
    return 0;
}
