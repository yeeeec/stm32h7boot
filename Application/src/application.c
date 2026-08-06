/**
 * @file application.c
 * @brief Top-level Bootloader policy and orchestration lifecycle.
 */
#include "application/application.h"
#include "application/application_config.h"

#include <stddef.h>
#include <string.h>

#include "services/capability/boot_control_service_api.h"
#include "services/capability/version_policy.h"
#include "services/use_case/active_validation_service_api.h"
#include "services/use_case/launch_service_api.h"
#include "services/use_case/recovery_service_api.h"
#include "services/use_case/update_service_api.h"

typedef enum
{
    APPLICATION_STAGE_IDLE = 0,
    APPLICATION_STAGE_RECOVERY_START,
    APPLICATION_STAGE_RECOVERY_PROCESS,
    APPLICATION_STAGE_WAIT_MEDIA,
    APPLICATION_STAGE_MOUNT_MEDIA,
    APPLICATION_STAGE_CHECK_REQUEST,
    APPLICATION_STAGE_PREPARE_START,
    APPLICATION_STAGE_PREPARE_PROCESS,
    APPLICATION_STAGE_DECIDE_PACKAGE,
    APPLICATION_STAGE_INSTALL_PROCESS,
    APPLICATION_STAGE_COMMIT_START,
    APPLICATION_STAGE_COMMIT_PROCESS,
    APPLICATION_STAGE_REMOVE_REQUEST,
    APPLICATION_STAGE_UNMOUNT_MEDIA,
    APPLICATION_STAGE_VALIDATE_START,
    APPLICATION_STAGE_VALIDATE_PROCESS,
    APPLICATION_STAGE_LAUNCH,
    APPLICATION_STAGE_RESET,
    APPLICATION_STAGE_FAILED
} application_stage_t;

static application_dependencies_t application_dependencies;
static boot_active_record_t application_active_record;
static boot_active_record_t application_candidate_record;
static application_stage_t application_stage;
static application_stage_t application_after_unmount;
static application_stage_t application_after_commit;
static application_stage_t application_commit_failure;
static int application_configured;
static int application_initialized;
static int application_media_mounted;
static int application_reset_after_cleanup;
static int application_recovery_attempted;
static int application_commit_is_recovery;

static void BeginUnmount(application_stage_t next)
{
    if (application_media_mounted != 0)
    {
        application_after_unmount = next;
        application_stage = APPLICATION_STAGE_UNMOUNT_MEDIA;
    }
    else
    {
        application_stage = next;
    }
}

static int IsAlreadyActive(const validated_manifest_t *manifest)
{
    return (memcmp(application_active_record.package_id_hash,
                   manifest->package_id_hash128,
                   sizeof(application_active_record.package_id_hash)) == 0) &&
           (memcmp(application_active_record.manifest_sha256,
                   manifest->manifest_sha256,
                   sizeof(application_active_record.manifest_sha256)) == 0);
}

firmware_status_t Application_Configure(
    const application_dependencies_t *dependencies)
{
    const package_source_t *source;

    if (dependencies == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    source = dependencies->package_source;
    if ((dependencies->boot_control == NULL) || (dependencies->update == NULL) ||
        (dependencies->recovery == NULL) ||
        (dependencies->validation == NULL) || (dependencies->launch == NULL) ||
        (source == NULL) || (source->is_media_present == NULL) ||
        (source->mount == NULL) || (source->unmount == NULL) ||
        (source->exists == NULL) || (source->remove == NULL) ||
        (dependencies->system_reset == NULL) ||
        (dependencies->system_reset->request == NULL) ||
        (dependencies->request_path == NULL))
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
    status = BootControlService_LoadActive(
        application_dependencies.boot_control, &application_active_record);
    if (!FirmwareStatus_IsOk(status) &&
        (status != FIRMWARE_STATUS_INVALID_STATE) &&
        (status != FIRMWARE_STATUS_OUT_OF_RANGE))
    {
        return status;
    }
    application_media_mounted = 0;
    application_reset_after_cleanup = 0;
    application_recovery_attempted = 0;
    application_commit_is_recovery = 0;
    application_stage = FirmwareStatus_IsOk(status)
                            ? APPLICATION_STAGE_WAIT_MEDIA
                            : APPLICATION_STAGE_RECOVERY_START;
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

    switch (application_stage)
    {
        case APPLICATION_STAGE_RECOVERY_START:
            status = RecoveryService_Start(
                application_dependencies.recovery, BOOT_PAIR_NONE);
            if (!FirmwareStatus_IsOk(status))
            {
                return status;
            }
            application_recovery_attempted = 1;
            application_stage = APPLICATION_STAGE_RECOVERY_PROCESS;
            break;

        case APPLICATION_STAGE_RECOVERY_PROCESS:
            RecoveryService_Process(application_dependencies.recovery);
            if (RecoveryService_GetState(application_dependencies.recovery) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                const boot_active_record_t *candidate =
                    RecoveryService_GetCandidate(application_dependencies.recovery);

                if (candidate == NULL)
                {
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
                application_candidate_record = *candidate;
                application_commit_is_recovery = 1;
                application_after_commit = APPLICATION_STAGE_VALIDATE_START;
                application_commit_failure = APPLICATION_STAGE_FAILED;
                application_stage = APPLICATION_STAGE_COMMIT_START;
            }
            else if (RecoveryService_GetState(application_dependencies.recovery) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *result =
                    RecoveryService_GetResult(application_dependencies.recovery);

                return (result == NULL) ? FIRMWARE_STATUS_INVALID_STATE
                                        : result->status;
            }
            break;

        case APPLICATION_STAGE_WAIT_MEDIA:
        {
            int present = 0;

            status = application_dependencies.package_source->is_media_present(
                application_dependencies.package_source->context, &present);
            application_stage = (FirmwareStatus_IsOk(status) && (present != 0))
                                    ? APPLICATION_STAGE_MOUNT_MEDIA
                                    : APPLICATION_STAGE_VALIDATE_START;
            break;
        }

        case APPLICATION_STAGE_MOUNT_MEDIA:
            status = application_dependencies.package_source->mount(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                application_media_mounted = 1;
                application_stage = APPLICATION_STAGE_CHECK_REQUEST;
            }
            else
            {
                application_stage = APPLICATION_STAGE_VALIDATE_START;
            }
            break;

        case APPLICATION_STAGE_CHECK_REQUEST:
        {
            int present = 0;

            status = application_dependencies.package_source->exists(
                application_dependencies.package_source->context,
                application_dependencies.request_path, &present);
            if (FirmwareStatus_IsOk(status) && (present != 0))
            {
                application_stage = APPLICATION_STAGE_PREPARE_START;
            }
            else
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
            }
            break;
        }

        case APPLICATION_STAGE_PREPARE_START:
            status = UpdateService_PrepareStart(application_dependencies.update);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_PREPARE_PROCESS;
            }
            else
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
            }
            break;

        case APPLICATION_STAGE_PREPARE_PROCESS:
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_stage = APPLICATION_STAGE_DECIDE_PACKAGE;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
            }
            break;

        case APPLICATION_STAGE_DECIDE_PACKAGE:
        {
            const validated_manifest_t *manifest =
                UpdateService_GetManifest(application_dependencies.update);

            if (manifest == NULL)
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
            if (IsAlreadyActive(manifest))
            {
                application_reset_after_cleanup = 0;
                application_stage = APPLICATION_STAGE_REMOVE_REQUEST;
                break;
            }
            if ((VersionPolicy_Compare(
                     &application_dependencies.bootloader_version,
                     &manifest->minimum_bootloader_version) < 0) ||
                !VersionPolicy_IsUpgrade(
                    &application_active_record.release_version,
                    &manifest->release_version))
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
                break;
            }
            status = UpdateService_InstallStart(
                application_dependencies.update, &application_active_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_INSTALL_PROCESS;
            }
            else
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
            }
            break;
        }

        case APPLICATION_STAGE_INSTALL_PROCESS:
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                const boot_active_record_t *candidate =
                    UpdateService_GetCandidate(application_dependencies.update);

                if (candidate == NULL)
                {
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
                application_candidate_record = *candidate;
                application_commit_is_recovery = 0;
                application_after_commit = APPLICATION_STAGE_REMOVE_REQUEST;
                application_commit_failure = APPLICATION_STAGE_VALIDATE_START;
                application_stage = APPLICATION_STAGE_COMMIT_START;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                BeginUnmount(APPLICATION_STAGE_VALIDATE_START);
            }
            break;

        case APPLICATION_STAGE_COMMIT_START:
            status = application_commit_is_recovery != 0
                         ? BootControlService_CommitRecoveredStart(
                               application_dependencies.boot_control,
                               &application_candidate_record)
                         : BootControlService_CommitActiveStart(
                               application_dependencies.boot_control,
                               &application_candidate_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_COMMIT_PROCESS;
            }
            else
            {
                BeginUnmount(application_commit_failure);
            }
            break;

        case APPLICATION_STAGE_COMMIT_PROCESS:
            BootControlService_Process(application_dependencies.boot_control);
            if (BootControlService_GetState(application_dependencies.boot_control) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_active_record = application_candidate_record;
                application_reset_after_cleanup =
                    (application_after_commit == APPLICATION_STAGE_REMOVE_REQUEST)
                        ? 1
                        : 0;
                application_stage = application_after_commit;
            }
            else if (BootControlService_GetState(application_dependencies.boot_control) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                BeginUnmount(application_commit_failure);
            }
            break;

        case APPLICATION_STAGE_REMOVE_REQUEST:
            /* Cleanup is best-effort after commit and for stale requests. */
            (void)application_dependencies.package_source->remove(
                application_dependencies.package_source->context,
                application_dependencies.request_path);
            BeginUnmount(application_reset_after_cleanup != 0
                             ? APPLICATION_STAGE_RESET
                             : APPLICATION_STAGE_VALIDATE_START);
            break;

        case APPLICATION_STAGE_UNMOUNT_MEDIA:
            (void)application_dependencies.package_source->unmount(
                application_dependencies.package_source->context);
            application_media_mounted = 0;
            application_stage = application_after_unmount;
            break;

        case APPLICATION_STAGE_VALIDATE_START:
            status = ActiveValidationService_Start(
                application_dependencies.validation, &application_active_record);
            if (!FirmwareStatus_IsOk(status))
            {
                return status;
            }
            application_stage = APPLICATION_STAGE_VALIDATE_PROCESS;
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
                if (application_recovery_attempted == 0)
                {
                    application_stage = APPLICATION_STAGE_RECOVERY_START;
                }
                else
                {
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
            }
            break;

        case APPLICATION_STAGE_LAUNCH:
            return LaunchService_Execute(
                application_dependencies.launch, &application_active_record);

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
