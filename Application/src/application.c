/**
 * @file application.c
 * @brief Top-level Application orchestration for trusted fixed-runtime updates.
 */
#include "application/application.h"
#include "application/application_config.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/update_request_service_api.h"
#include "services/capability/version_policy.h"
#include "services/use_case/active_validation_service_api.h"
#include "services/use_case/launch_service_api.h"
#include "services/use_case/update_service_api.h"

typedef enum
{
    APPLICATION_STAGE_STARTUP = 0,
    APPLICATION_STAGE_UPDATE_CHECK,
    APPLICATION_STAGE_MOUNT,
    APPLICATION_STAGE_REQUEST_LOAD,
    APPLICATION_STAGE_PREPARE_START,
    APPLICATION_STAGE_PREPARE_PROCESS,
    APPLICATION_STAGE_POLICY,
    APPLICATION_STAGE_STALE_VALIDATE_START,
    APPLICATION_STAGE_STALE_VALIDATE_PROCESS,
    APPLICATION_STAGE_INSTALL_START,
    APPLICATION_STAGE_INSTALL_PROCESS,
    APPLICATION_STAGE_COMMIT_START,
    APPLICATION_STAGE_COMMIT_PROCESS,
    APPLICATION_STAGE_CLEANUP,
    APPLICATION_STAGE_UNMOUNT,
    APPLICATION_STAGE_VALIDATE_START,
    APPLICATION_STAGE_VALIDATE_PROCESS,
    APPLICATION_STAGE_LAUNCH,
    APPLICATION_STAGE_RESET,
    APPLICATION_STAGE_FAILED
} application_stage_t;

static application_dependencies_t application_dependencies;
static boot_active_record_t application_active_record;
static boot_active_record_t application_candidate_record;
static update_request_t application_request;
static uint8_t application_request_raw[UPDATE_REQUEST_STORE_MAX_RAW_SIZE];
static uint32_t application_request_raw_size;
static application_stage_t application_stage;
static application_stage_t application_after_unmount;
static int application_configured;
static int application_initialized;
static int application_media_mounted;
static int application_has_active_record;
static int application_stale_request;
static int application_reset_after_cleanup;
static int application_stage_logged;
static application_stage_t application_logged_stage;

static const char *ApplicationStageName(application_stage_t stage)
{
    static const char *const names[] = {
        "startup", "update-check", "mount", "request-load", "prepare-start",
        "prepare-process", "policy", "stale-validate-start", "stale-validate-process",
        "install-start", "install-process", "commit-start", "commit-process", "cleanup",
        "unmount", "validate-start", "validate-process", "launch", "reset", "failed"};
    return ((unsigned int)stage < (sizeof(names) / sizeof(names[0]))) ? names[stage] : "unknown";
}

static void LogStageEntry(void)
{
    if ((application_stage_logged == 0) || (application_logged_stage != application_stage))
    {
        LOG_INFO("app", "stage=%s", ApplicationStageName(application_stage));
        application_logged_stage = application_stage;
        application_stage_logged = 1;
    }
}

static int HasActiveRecord(void)
{
    return application_has_active_record != 0;
}

static void BeginUnmount(application_stage_t next)
{
    application_after_unmount = next;
    application_stage = (application_media_mounted != 0) ? APPLICATION_STAGE_UNMOUNT : next;
}

static void FailClosed(void)
{
    application_stage = APPLICATION_STAGE_FAILED;
}

static void ContinueCurrentRuntime(void)
{
    BeginUnmount(HasActiveRecord() ? APPLICATION_STAGE_VALIDATE_START : APPLICATION_STAGE_FAILED);
}

static int IsSamePackage(const validated_manifest_t *manifest)
{
    return HasActiveRecord() && (manifest != NULL) &&
           (memcmp(application_active_record.package_id_hash, manifest->package_id_hash128,
                   sizeof(application_active_record.package_id_hash)) == 0) &&
           (memcmp(application_active_record.manifest_sha256, manifest->manifest_sha256,
                   sizeof(application_active_record.manifest_sha256)) == 0);
}

static void StartValidationOrFault(void)
{
    application_stage = HasActiveRecord() ? APPLICATION_STAGE_VALIDATE_START
                                           : APPLICATION_STAGE_FAILED;
}

firmware_status_t Application_Configure(const application_dependencies_t *dependencies)
{
    const package_source_t *source;
    const update_request_store_t *store;

    if (dependencies == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    source = dependencies->package_source;
    store = dependencies->update_request_store;
    if ((dependencies->boot_control == NULL) || (dependencies->update == NULL) ||
        (dependencies->validation == NULL) || (dependencies->launch == NULL) ||
        (dependencies->update_request_service == NULL) || (source == NULL) ||
        (source->is_media_present == NULL) || (source->mount == NULL) ||
        (source->unmount == NULL) || (store == NULL) || (store->load_raw == NULL) ||
        (store->clear == NULL) || (dependencies->system_reset == NULL) ||
        (dependencies->system_reset->request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((application_configured != 0) || (application_initialized != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    application_dependencies = *dependencies;
    application_configured = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Init(void)
{
    firmware_status_t status;

    if ((application_initialized != 0) || (application_configured == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BootControlService_LoadActive(application_dependencies.boot_control,
                                            &application_active_record);
    if (!FirmwareStatus_IsOk(status) && (status != FIRMWARE_STATUS_INVALID_STATE) &&
        (status != FIRMWARE_STATUS_OUT_OF_RANGE) && (status != FIRMWARE_STATUS_NOT_FOUND))
    {
        application_stage = APPLICATION_STAGE_FAILED;
        return status;
    }
    application_has_active_record = FirmwareStatus_IsOk(status) ? 1 : 0;
    application_media_mounted = 0;
    application_stale_request = 0;
    application_reset_after_cleanup = 0;
    application_request_raw_size = 0U;
    application_stage = APPLICATION_STAGE_STARTUP;
    application_stage_logged = 0;
    application_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Process(void)
{
    firmware_status_t status;

    if (application_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    LogStageEntry();

    switch (application_stage)
    {
        case APPLICATION_STAGE_STARTUP:
            /* Active Record loading is complete; every boot now checks the trusted request. */
            application_stage = APPLICATION_STAGE_UPDATE_CHECK;
            break;

        case APPLICATION_STAGE_UPDATE_CHECK:
        {
            int present = 0;
            status = application_dependencies.package_source->is_media_present(
                application_dependencies.package_source->context, &present);
            if (!FirmwareStatus_IsOk(status))
            {
                StartValidationOrFault();
            }
            else if (present == 0)
            {
                StartValidationOrFault();
            }
            else
            {
                application_stage = APPLICATION_STAGE_MOUNT;
            }
            break;
        }

        case APPLICATION_STAGE_MOUNT:
            status = application_dependencies.package_source->mount(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                application_media_mounted = 1;
                application_stage = APPLICATION_STAGE_REQUEST_LOAD;
            }
            else
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_REQUEST_LOAD:
            application_request_raw_size = 0U;
            status = application_dependencies.update_request_store->load_raw(
                application_dependencies.update_request_store->context,
                application_request_raw, sizeof(application_request_raw),
                &application_request_raw_size);
            if (status == FIRMWARE_STATUS_NOT_FOUND)
            {
                LOG_WARN("app", "trusted request not found: status=%d", (int)status);
                ContinueCurrentRuntime();
            }
            else if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("app", "trusted request load failed: status=%d", (int)status);
                ContinueCurrentRuntime();
            }
            else
            {
                application_stage = APPLICATION_STAGE_PREPARE_START;
            }
            break;

        case APPLICATION_STAGE_PREPARE_START:
            status = UpdateRequestService_ParseAndValidate(
                application_dependencies.update_request_service, application_request_raw,
                application_request_raw_size, &application_request);
            if (!FirmwareStatus_IsOk(status))
            {
                ContinueCurrentRuntime();
            }
            else
            {
                status = UpdateService_PrepareStart(application_dependencies.update,
                                                    &application_request);
                if (FirmwareStatus_IsOk(status))
                {
                    application_stage = APPLICATION_STAGE_PREPARE_PROCESS;
                }
                else
                {
                    ContinueCurrentRuntime();
                }
            }
            break;

        case APPLICATION_STAGE_PREPARE_PROCESS:
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_stage = APPLICATION_STAGE_POLICY;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_POLICY:
        {
            const validated_manifest_t *manifest =
                UpdateService_GetManifest(application_dependencies.update);
            if (manifest == NULL)
            {
                ContinueCurrentRuntime();
                break;
            }
            if (VersionPolicy_Compare(&application_dependencies.bootloader_version,
                                      &manifest->minimum_bootloader_version) < 0)
            {
                ContinueCurrentRuntime();
                break;
            }
            application_stale_request = IsSamePackage(manifest);
            if (application_stale_request != 0)
            {
                application_stage = APPLICATION_STAGE_STALE_VALIDATE_START;
            }
            else if (HasActiveRecord() &&
                     !VersionPolicy_IsUpgrade(&application_active_record.release_version,
                                              &manifest->release_version))
            {
                ContinueCurrentRuntime();
            }
            else
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;
        }

        case APPLICATION_STAGE_STALE_VALIDATE_START:
            status = ActiveValidationService_Start(application_dependencies.validation,
                                                   &application_active_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_STALE_VALIDATE_PROCESS;
            }
            else
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;

        case APPLICATION_STAGE_STALE_VALIDATE_PROCESS:
            ActiveValidationService_Process(application_dependencies.validation);
            if (ActiveValidationService_GetState(application_dependencies.validation) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_reset_after_cleanup = 0;
                application_stage = APPLICATION_STAGE_CLEANUP;
            }
            else if (ActiveValidationService_GetState(application_dependencies.validation) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;

        case APPLICATION_STAGE_INSTALL_START:
            status = UpdateService_InstallStart(application_dependencies.update);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_INSTALL_PROCESS;
            }
            else
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_INSTALL_PROCESS:
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                const boot_active_record_t *candidate =
                    UpdateService_GetCandidate(application_dependencies.update);
                if (candidate == NULL)
                {
                    FailClosed();
                }
                else
                {
                    application_candidate_record = *candidate;
                    application_reset_after_cleanup = 1;
                    application_stage = APPLICATION_STAGE_COMMIT_START;
                }
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                if (UpdateService_RuntimeMayBeModified(application_dependencies.update) != 0)
                {
                    BeginUnmount(APPLICATION_STAGE_FAILED);
                }
                else
                {
                    ContinueCurrentRuntime();
                }
            }
            break;

        case APPLICATION_STAGE_COMMIT_START:
            status = BootControlService_CommitActiveStart(application_dependencies.boot_control,
                                                           &application_candidate_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_COMMIT_PROCESS;
            }
            else
            {
                BeginUnmount(APPLICATION_STAGE_FAILED);
            }
            break;

        case APPLICATION_STAGE_COMMIT_PROCESS:
            BootControlService_Process(application_dependencies.boot_control);
            if (BootControlService_GetState(application_dependencies.boot_control) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_active_record = application_candidate_record;
                application_has_active_record = 1;
                application_stage = APPLICATION_STAGE_CLEANUP;
            }
            else if (BootControlService_GetState(application_dependencies.boot_control) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                BeginUnmount(APPLICATION_STAGE_FAILED);
            }
            break;

        case APPLICATION_STAGE_CLEANUP:
            status = application_dependencies.update_request_store->clear(
                application_dependencies.update_request_store->context);
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("app", "request clear failed: status=%d", (int)status);
            }
            BeginUnmount(application_reset_after_cleanup != 0 ? APPLICATION_STAGE_RESET
                                                               : APPLICATION_STAGE_VALIDATE_START);
            break;

        case APPLICATION_STAGE_UNMOUNT:
            status = application_dependencies.package_source->unmount(
                application_dependencies.package_source->context);
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("app", "media unmount failed: status=%d", (int)status);
            }
            application_media_mounted = 0;
            application_stage = application_after_unmount;
            break;

        case APPLICATION_STAGE_VALIDATE_START:
            if (!HasActiveRecord())
            {
                FailClosed();
                break;
            }
            status = ActiveValidationService_Start(application_dependencies.validation,
                                                   &application_active_record);
            application_stage = FirmwareStatus_IsOk(status)
                                    ? APPLICATION_STAGE_VALIDATE_PROCESS
                                    : APPLICATION_STAGE_FAILED;
            break;

        case APPLICATION_STAGE_VALIDATE_PROCESS:
            ActiveValidationService_Process(application_dependencies.validation);
            if (ActiveValidationService_GetState(application_dependencies.validation) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_stage = APPLICATION_STAGE_LAUNCH;
            }
            else if (ActiveValidationService_GetState(application_dependencies.validation) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                FailClosed();
            }
            break;

        case APPLICATION_STAGE_LAUNCH:
            status = LaunchService_Execute(application_dependencies.launch,
                                           &application_active_record);
            if (!FirmwareStatus_IsOk(status))
            {
                FailClosed();
            }
            break;

        case APPLICATION_STAGE_RESET:
            application_dependencies.system_reset->request(
                application_dependencies.system_reset->context);
            break;

        case APPLICATION_STAGE_FAILED:
            return FIRMWARE_STATUS_INVALID_STATE;

        default:
            return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
