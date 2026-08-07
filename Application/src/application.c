/**
 * @file application.c
 * @brief Top-level Bootloader policy and orchestration lifecycle.
 */
#include "application/application.h"
#include "application/application_config.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
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
static int application_has_active_record;
static int application_reset_after_cleanup;
static int application_recovery_attempted;
static int application_commit_is_recovery;
static int application_wait_for_media_removal;
static int application_stage_logged;
static application_stage_t application_logged_stage;

static const char *ApplicationStageName(application_stage_t stage)
{
    switch (stage)
    {
        case APPLICATION_STAGE_IDLE:
            return "idle";
        case APPLICATION_STAGE_RECOVERY_START:
            return "recovery-start";
        case APPLICATION_STAGE_RECOVERY_PROCESS:
            return "recovery-process";
        case APPLICATION_STAGE_WAIT_MEDIA:
            return "wait-media";
        case APPLICATION_STAGE_MOUNT_MEDIA:
            return "mount-media";
        case APPLICATION_STAGE_CHECK_REQUEST:
            return "check-request";
        case APPLICATION_STAGE_PREPARE_START:
            return "prepare-start";
        case APPLICATION_STAGE_PREPARE_PROCESS:
            return "prepare-process";
        case APPLICATION_STAGE_DECIDE_PACKAGE:
            return "decide-package";
        case APPLICATION_STAGE_INSTALL_PROCESS:
            return "install-process";
        case APPLICATION_STAGE_COMMIT_START:
            return "commit-start";
        case APPLICATION_STAGE_COMMIT_PROCESS:
            return "commit-process";
        case APPLICATION_STAGE_REMOVE_REQUEST:
            return "remove-request";
        case APPLICATION_STAGE_UNMOUNT_MEDIA:
            return "unmount-media";
        case APPLICATION_STAGE_VALIDATE_START:
            return "validate-start";
        case APPLICATION_STAGE_VALIDATE_PROCESS:
            return "validate-process";
        case APPLICATION_STAGE_LAUNCH:
            return "launch";
        case APPLICATION_STAGE_RESET:
            return "reset";
        case APPLICATION_STAGE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

static const char *BootPairName(boot_pair_t pair)
{
    switch (pair)
    {
        case BOOT_PAIR_NONE:
            return "none";
        case BOOT_PAIR_1:
            return "pair-1";
        case BOOT_PAIR_2:
            return "pair-2";
        default:
            return "unknown";
    }
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

static void BeginUnmount(application_stage_t next)
{
    if (next == APPLICATION_STAGE_WAIT_MEDIA)
    {
        application_wait_for_media_removal = 1;
    }
    if (application_media_mounted != 0)
    {
        application_after_unmount = next;
        application_stage         = APPLICATION_STAGE_UNMOUNT_MEDIA;
    }
    else
    {
        application_stage = next;
    }
}

static int IsAlreadyActive(const validated_manifest_t *manifest)
{
    return (memcmp(application_active_record.package_id_hash, manifest->package_id_hash128,
                   sizeof(application_active_record.package_id_hash)) == 0) &&
           (memcmp(application_active_record.manifest_sha256, manifest->manifest_sha256,
                   sizeof(application_active_record.manifest_sha256)) == 0);
}

firmware_status_t Application_Configure(const application_dependencies_t *dependencies)
{
    const package_source_t *source;

    if (dependencies == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    source = dependencies->package_source;
    if ((dependencies->boot_control == NULL) || (dependencies->update == NULL) ||
        (dependencies->recovery == NULL) || (dependencies->validation == NULL) ||
        (dependencies->launch == NULL) || (source == NULL) || (source->is_media_present == NULL) ||
        (source->mount == NULL) || (source->unmount == NULL) || (source->exists == NULL) ||
        (source->remove == NULL) || (dependencies->system_reset == NULL) ||
        (dependencies->system_reset->request == NULL) || (dependencies->request_path == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((application_configured != 0) || (application_initialized != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    application_dependencies = *dependencies;
    application_configured   = 1;
    LOG_INFO("app", "configured: request=%s", application_dependencies.request_path);
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
        (status != FIRMWARE_STATUS_OUT_OF_RANGE))
    {
        return status;
    }
    application_media_mounted          = 0;
    application_has_active_record      = FirmwareStatus_IsOk(status) ? 1 : 0;
    application_reset_after_cleanup    = 0;
    application_recovery_attempted     = 0;
    application_commit_is_recovery     = 0;
    application_wait_for_media_removal = 0;
    application_stage        = FirmwareStatus_IsOk(status) ? APPLICATION_STAGE_WAIT_MEDIA
                                                           : APPLICATION_STAGE_RECOVERY_START;
    application_stage_logged = 0;
    application_initialized  = 1;
    if (application_has_active_record != 0)
    {
        LOG_INFO("app", "active record loaded: pair=%s version=%u.%u.%u build=%lu",
                 BootPairName(application_active_record.active_pair),
                 (unsigned int) application_active_record.release_version.major,
                 (unsigned int) application_active_record.release_version.minor,
                 (unsigned int) application_active_record.release_version.patch,
                 (unsigned long) application_active_record.build_number);
    }
    else
    {
        LOG_WARN("app", "no active record: status=%d, entering recovery", (int) status);
    }
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
        case APPLICATION_STAGE_RECOVERY_START:
            status = RecoveryService_Start(application_dependencies.recovery, BOOT_PAIR_NONE);
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_ERROR("app", "recovery start failed: status=%d", (int) status);
                return status;
            }
            application_recovery_attempted = 1;
            application_stage              = APPLICATION_STAGE_RECOVERY_PROCESS;
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
                LOG_INFO("app", "recovery candidate selected: pair=%s sequence=%lu",
                         BootPairName(application_candidate_record.active_pair),
                         (unsigned long) application_candidate_record.sequence);
                application_commit_is_recovery = 1;
                application_after_commit       = APPLICATION_STAGE_VALIDATE_START;
                application_commit_failure     = APPLICATION_STAGE_FAILED;
                application_stage              = APPLICATION_STAGE_COMMIT_START;
            }
            else if (RecoveryService_GetState(application_dependencies.recovery) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *result =
                    RecoveryService_GetResult(application_dependencies.recovery);

                if ((application_has_active_record == 0) && (result != NULL) &&
                    (result->error == BOOT_ERROR_NO_VALID_PAIR))
                {
                    /* An unprovisioned device may wait for its first package. */
                    LOG_WARN("app", "recovery found no valid pair; waiting for package");
                    application_stage = APPLICATION_STAGE_WAIT_MEDIA;
                }
                else
                {
                    LOG_ERROR("app", "recovery failed: status=%d error=%d stage=%lu",
                              (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE
                                               : (int) result->status,
                              (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                              (result == NULL) ? 0UL : (unsigned long) result->stage);
                    return (result == NULL) ? FIRMWARE_STATUS_INVALID_STATE : result->status;
                }
            }
            break;

        case APPLICATION_STAGE_WAIT_MEDIA:
        {
            int present = 0;

            status = application_dependencies.package_source->is_media_present(
                application_dependencies.package_source->context, &present);
            if (FirmwareStatus_IsOk(status) && (present == 0))
            {
                application_wait_for_media_removal = 0;
            }
            if (FirmwareStatus_IsOk(status) && (present != 0) &&
                (application_wait_for_media_removal == 0))
            {
                LOG_INFO("app", "update media detected");
                application_stage = APPLICATION_STAGE_MOUNT_MEDIA;
            }
            else if (application_has_active_record == 0)
            {
                /* Stay available for a package inserted after reset. */
                application_stage = APPLICATION_STAGE_WAIT_MEDIA;
            }
            else
            {
                if (!FirmwareStatus_IsOk(status))
                {
                    LOG_WARN("app", "media probe failed: status=%d", (int) status);
                }
                application_stage = APPLICATION_STAGE_VALIDATE_START;
            }
            break;
        }

        case APPLICATION_STAGE_MOUNT_MEDIA:
            status = application_dependencies.package_source->mount(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                application_media_mounted          = 1;
                application_wait_for_media_removal = 0;
                LOG_INFO("app", "update media mounted");
                application_stage = APPLICATION_STAGE_CHECK_REQUEST;
            }
            else
            {
                application_wait_for_media_removal = 1;
                LOG_WARN("app", "media mount failed: status=%d", (int) status);
                application_stage = (application_has_active_record != 0)
                                        ? APPLICATION_STAGE_VALIDATE_START
                                        : APPLICATION_STAGE_WAIT_MEDIA;
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
                LOG_INFO("app", "update request found: %s", application_dependencies.request_path);
                application_stage = APPLICATION_STAGE_PREPARE_START;
            }
            else
            {
                if (FirmwareStatus_IsOk(status))
                {
                    LOG_INFO("app", "no update request found");
                }
                else
                {
                    LOG_WARN("app", "request probe failed: status=%d", (int) status);
                }
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_WAIT_MEDIA);
            }
            break;
        }

        case APPLICATION_STAGE_PREPARE_START:
            status = UpdateService_PrepareStart(application_dependencies.update);
            if (FirmwareStatus_IsOk(status))
            {
                LOG_INFO("app", "update prepare started");
                application_stage = APPLICATION_STAGE_PREPARE_PROCESS;
            }
            else
            {
                LOG_WARN("app", "update prepare start failed: status=%d", (int) status);
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_WAIT_MEDIA);
            }
            break;

        case APPLICATION_STAGE_PREPARE_PROCESS:
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                LOG_INFO("app", "update manifest prepared");
                application_stage = APPLICATION_STAGE_DECIDE_PACKAGE;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *result =
                    UpdateService_GetResult(application_dependencies.update);

                LOG_WARN("app", "update prepare failed: status=%d error=%d stage=%lu",
                         (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE
                                          : (int) result->status,
                         (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                         (result == NULL) ? 0UL : (unsigned long) result->stage);
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_WAIT_MEDIA);
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
            if ((application_has_active_record != 0) && IsAlreadyActive(manifest))
            {
                LOG_INFO("app", "package already active: version=%u.%u.%u build=%lu",
                         (unsigned int) manifest->release_version.major,
                         (unsigned int) manifest->release_version.minor,
                         (unsigned int) manifest->release_version.patch,
                         (unsigned long) manifest->build_number);
                application_reset_after_cleanup = 0;
                application_stage               = APPLICATION_STAGE_REMOVE_REQUEST;
                break;
            }
            if ((VersionPolicy_Compare(&application_dependencies.bootloader_version,
                                       &manifest->minimum_bootloader_version) < 0) ||
                ((application_has_active_record != 0) &&
                 !VersionPolicy_IsUpgrade(&application_active_record.release_version,
                                          &manifest->release_version)))
            {
                LOG_WARN("app", "package rejected by version policy: version=%u.%u.%u build=%lu",
                         (unsigned int) manifest->release_version.major,
                         (unsigned int) manifest->release_version.minor,
                         (unsigned int) manifest->release_version.patch,
                         (unsigned long) manifest->build_number);
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_WAIT_MEDIA);
                break;
            }
            status = (application_has_active_record != 0)
                         ? UpdateService_InstallStart(application_dependencies.update,
                                                      &application_active_record)
                         : UpdateService_InitialInstallStart(application_dependencies.update,
                                                             BOOT_PAIR_1);
            if (FirmwareStatus_IsOk(status))
            {
                LOG_INFO("app", "update install started");
                application_stage = APPLICATION_STAGE_INSTALL_PROCESS;
            }
            else
            {
                LOG_WARN("app", "update install start failed: status=%d", (int) status);
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_WAIT_MEDIA);
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
                LOG_INFO("app", "update install completed: target=%s app=%lu gui=%lu",
                         BootPairName(application_candidate_record.active_pair),
                         (unsigned long) application_candidate_record.app_size,
                         (unsigned long) application_candidate_record.gui_size);
                application_commit_is_recovery = 0;
                application_after_commit       = APPLICATION_STAGE_REMOVE_REQUEST;
                application_commit_failure     = (application_has_active_record != 0)
                                                     ? APPLICATION_STAGE_VALIDATE_START
                                                     : APPLICATION_STAGE_FAILED;
                application_stage              = APPLICATION_STAGE_COMMIT_START;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *result =
                    UpdateService_GetResult(application_dependencies.update);

                LOG_WARN("app", "update install failed: status=%d error=%d stage=%lu",
                         (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE
                                          : (int) result->status,
                         (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                         (result == NULL) ? 0UL : (unsigned long) result->stage);
                BeginUnmount((application_has_active_record != 0) ? APPLICATION_STAGE_VALIDATE_START
                                                                  : APPLICATION_STAGE_FAILED);
            }
            break;

        case APPLICATION_STAGE_COMMIT_START:
            status =
                application_commit_is_recovery != 0
                    ? BootControlService_CommitRecoveredStart(application_dependencies.boot_control,
                                                              &application_candidate_record)
                    : BootControlService_CommitActiveStart(application_dependencies.boot_control,
                                                           &application_candidate_record);
            if (FirmwareStatus_IsOk(status))
            {
                LOG_INFO("app", "active record commit started: recovery=%d target=%s",
                         application_commit_is_recovery,
                         BootPairName(application_candidate_record.active_pair));
                application_stage = APPLICATION_STAGE_COMMIT_PROCESS;
            }
            else
            {
                LOG_ERROR("app", "active record commit start failed: status=%d", (int) status);
                BeginUnmount(application_commit_failure);
            }
            break;

        case APPLICATION_STAGE_COMMIT_PROCESS:
            BootControlService_Process(application_dependencies.boot_control);
            if (BootControlService_GetState(application_dependencies.boot_control) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_active_record     = application_candidate_record;
                application_has_active_record = 1;
                application_reset_after_cleanup =
                    (application_after_commit == APPLICATION_STAGE_REMOVE_REQUEST) ? 1 : 0;
                LOG_INFO("app", "active record committed: pair=%s",
                         BootPairName(application_active_record.active_pair));
                application_stage = application_after_commit;
            }
            else if (BootControlService_GetState(application_dependencies.boot_control) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *result =
                    BootControlService_GetResult(application_dependencies.boot_control);

                LOG_ERROR("app", "active record commit failed: status=%d error=%d stage=%lu",
                          (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE
                                           : (int) result->status,
                          (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                          (result == NULL) ? 0UL : (unsigned long) result->stage);
                BeginUnmount(application_commit_failure);
            }
            break;

        case APPLICATION_STAGE_REMOVE_REQUEST:
            /* Cleanup is best-effort after commit and for stale requests. */
            (void) application_dependencies.package_source->remove(
                application_dependencies.package_source->context,
                application_dependencies.request_path);
            LOG_INFO("app", "update request cleanup requested");
            BeginUnmount(application_reset_after_cleanup != 0 ? APPLICATION_STAGE_RESET
                                                              : APPLICATION_STAGE_VALIDATE_START);
            break;

        case APPLICATION_STAGE_UNMOUNT_MEDIA:
            (void) application_dependencies.package_source->unmount(
                application_dependencies.package_source->context);
            application_media_mounted = 0;
            LOG_INFO("app", "update media unmounted");
            application_stage = application_after_unmount;
            break;

        case APPLICATION_STAGE_VALIDATE_START:
            status = ActiveValidationService_Start(application_dependencies.validation,
                                                   &application_active_record);
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_ERROR("app", "active validation start failed: status=%d", (int) status);
                return status;
            }
            LOG_INFO("app", "active validation started: pair=%s",
                     BootPairName(application_active_record.active_pair));
            application_stage = APPLICATION_STAGE_VALIDATE_PROCESS;
            break;

        case APPLICATION_STAGE_VALIDATE_PROCESS:
            ActiveValidationService_Process(application_dependencies.validation);
            if (ActiveValidationService_GetState(application_dependencies.validation) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                LOG_INFO("app", "active validation succeeded");
                application_stage = APPLICATION_STAGE_LAUNCH;
            }
            else if (ActiveValidationService_GetState(application_dependencies.validation) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                if (application_recovery_attempted == 0)
                {
                    const service_result_t *result =
                        ActiveValidationService_GetResult(application_dependencies.validation);

                    LOG_WARN(
                        "app",
                        "active validation failed, trying recovery: status=%d error=%d stage=%lu",
                        (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE
                                         : (int) result->status,
                        (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                        (result == NULL) ? 0UL : (unsigned long) result->stage);
                    application_stage = APPLICATION_STAGE_RECOVERY_START;
                }
                else
                {
                    LOG_ERROR("app", "active validation failed after recovery");
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
            }
            break;

        case APPLICATION_STAGE_LAUNCH:
            LOG_INFO("app", "launching active pair=%s",
                     BootPairName(application_active_record.active_pair));
            return LaunchService_Execute(application_dependencies.launch,
                                         &application_active_record);

        case APPLICATION_STAGE_RESET:
            LOG_WARN("app", "requesting reset after update cleanup");
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
