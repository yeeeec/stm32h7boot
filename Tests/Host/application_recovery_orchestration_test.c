#include <assert.h>
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
static boot_active_record_t loaded_active_record;
static boot_active_record_t recovery_candidate;
static service_run_state_t recovery_state;
static service_run_state_t commit_state;
static service_run_state_t validation_state;
static uint32_t recovery_count;
static uint32_t commit_count;
static uint32_t normal_commit_count;
static uint32_t validation_count;
static uint32_t launch_count;
static uint32_t reset_count;
static uint32_t update_count;
static uint16_t validating_major;

static firmware_status_t MediaPresent(void *context, int *present)
{
    (void)context;
    *present = 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SourceOperation(void *context)
{
    (void)context;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Exists(void *context, const char *path, int *present)
{
    (void)context;
    (void)path;
    *present = 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Remove(void *context, const char *path)
{
    (void)context;
    (void)path;
    return FIRMWARE_STATUS_OK;
}

static void Reset(void *context)
{
    (void)context;
    ++reset_count;
}

firmware_status_t BootControlService_LoadActive(
    struct boot_control_service *service, boot_active_record_t *record)
{
    (void)service;
#if defined(TEST_ACTIVE_VALIDATION_FAILURE)
    *record = loaded_active_record;
    return FIRMWARE_STATUS_OK;
#else
    (void)record;
    return FIRMWARE_STATUS_INVALID_STATE;
#endif
}

firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service, const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++normal_commit_count;
    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t BootControlService_CommitRecoveredStart(
    struct boot_control_service *service, const boot_active_record_t *record)
{
    (void)service;
    validating_major = record->release_version.major;
    ++commit_count;
    commit_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
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

firmware_status_t RecoveryService_Start(
    struct recovery_service *service, boot_pair_t preferred_pair)
{
    (void)service;
    assert(preferred_pair == BOOT_PAIR_NONE);
    ++recovery_count;
    recovery_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void RecoveryService_Process(struct recovery_service *service)
{
    (void)service;
    recovery_state = SERVICE_RUN_STATE_SUCCEEDED;
}

service_run_state_t RecoveryService_GetState(
    const struct recovery_service *service)
{
    (void)service;
    return recovery_state;
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
    return &recovery_candidate;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service)
{
    (void)service;
    ++update_count;
    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t UpdateService_InstallStart(
    struct update_service *service, const boot_active_record_t *record)
{
    (void)service;
    (void)record;
    ++update_count;
    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t UpdateService_InitialInstallStart(
    struct update_service *service, boot_pair_t target_pair)
{
    (void)service;
    (void)target_pair;
    return FIRMWARE_STATUS_INVALID_STATE;
}

void UpdateService_Process(struct update_service *service)
{
    (void)service;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    (void)service;
    return SERVICE_RUN_STATE_FAILED;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    (void)service;
    return NULL;
}

const validated_manifest_t *UpdateService_GetManifest(
    const struct update_service *service)
{
    (void)service;
    return NULL;
}

const boot_active_record_t *UpdateService_GetCandidate(
    const struct update_service *service)
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
    validating_major = record->release_version.major;
    ++validation_count;
    validation_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void ActiveValidationService_Process(struct active_validation_service *service)
{
    (void)service;
    validation_state = (validating_major == 1U)
                           ? SERVICE_RUN_STATE_FAILED
                           : SERVICE_RUN_STATE_SUCCEEDED;
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
    ++launch_count;
    return FIRMWARE_STATUS_OK;
}

const service_result_t *LaunchService_GetResult(
    const struct launch_service *service)
{
    (void)service;
    return NULL;
}

int main(void)
{
    package_source_t source;
    system_reset_t system_reset;
    application_dependencies_t dependencies;
    uint32_t step;

    memset(&source, 0, sizeof(source));
    memset(&system_reset, 0, sizeof(system_reset));
    memset(&dependencies, 0, sizeof(dependencies));
    memset(&loaded_active_record, 0, sizeof(loaded_active_record));
    memset(&recovery_candidate, 0, sizeof(recovery_candidate));
    loaded_active_record.release_version.major = 1U;
    loaded_active_record.app_size = 1U;
    loaded_active_record.gui_size = 1U;
    recovery_candidate.release_version.major = 2U;
    recovery_candidate.app_size = 1U;
    recovery_candidate.gui_size = 1U;

    source.is_media_present = MediaPresent;
    source.mount = SourceOperation;
    source.unmount = SourceOperation;
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
    for (step = 0U; (step < 16U) && (launch_count == 0U); ++step)
    {
        assert(Application_Process() == FIRMWARE_STATUS_OK);
    }
    assert(recovery_count == 1U);
    assert(commit_count == 1U);
    assert(normal_commit_count == 0U);
#if defined(TEST_ACTIVE_VALIDATION_FAILURE)
    assert(validation_count == 2U);
#else
    assert(validation_count == 1U);
#endif
    assert(launch_count == 1U);
    assert(reset_count == 0U);
    assert(update_count == 0U);
    return 0;
}
