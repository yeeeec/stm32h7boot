#include "update/update_service.h"

#include <string.h>

#include "bootloader_config.h"
#include "logging.h"
#include "platform/platform_nv_storage.h"
#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "update_config.h"
#include "update_internal.h"

static update_result_t result(update_outcome_t outcome, update_failure_t failure,
                              firmware_status_t status)
{
    update_result_t value = {outcome, failure, status};
    return value;
}

#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
static const char *source_name(uint32_t source)
{
    switch (source)
    {
        case UPDATE_SOURCE_NONE:
            return "none";
        case UPDATE_SOURCE_CANDIDATE:
            return "candidate";
        case UPDATE_SOURCE_ROLLBACK:
            return "rollback";
        default:
            return "unknown";
    }
}
#endif

static uint8_t s_update_initialized;

firmware_status_t UpdateService_Init(void)
{
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
    update_journal_record_t journal;
    firmware_status_t status;
    if (s_update_initialized != 0U)
        return FIRMWARE_STATUS_OK;
    status = PlatformNvStorage_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
        (void) memset(&journal, 0, sizeof(journal));
        journal.state  = UPDATE_STATE_IDLE;
        journal.source = UPDATE_SOURCE_NONE;
        status         = UpdateJournal_Write(&journal);
    }
    if (FirmwareStatus_IsError(status))
        return status;
#endif
    s_update_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t mount_storage(void)
{
    firmware_status_t status = PlatformStorage_Init();
    return FirmwareStatus_IsOk(status) ? PlatformStorage_Mount() : status;
}

#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
static void set_error(update_journal_record_t *journal, update_failure_t failure)
{
    journal->last_error = (uint32_t) failure;
}

static void clear_candidate_transaction(update_journal_record_t *journal)
{
    journal->candidate_version = 0U;
    journal->component_mask    = 0U;
    (void) memset(journal->candidate_package_id, 0, sizeof(journal->candidate_package_id));
    (void) memset(journal->candidate_manifest_sha256, 0,
                  sizeof(journal->candidate_manifest_sha256));
}

static update_result_t fail(update_journal_record_t *journal, update_failure_t failure,
                            firmware_status_t status, int persist_failed)
{
    if (journal != NULL)
    {
        set_error(journal, failure);
        if (persist_failed != 0)
        {
            journal->state = UPDATE_STATE_FAILED;
            (void) UpdateJournal_Write(journal);
        }
    }
    return result(persist_failed != 0 ? UPDATE_OUTCOME_RUNTIME_UNSAFE : UPDATE_OUTCOME_NO_CHANGE,
                  failure, status);
}

static update_result_t process_installing(update_journal_record_t *journal);

static update_result_t process_commit_pending(update_journal_record_t *journal)
{
    update_request_t request;
    update_package_t package;
    update_package_t current;
    update_journal_record_t pending_record;
    update_operation_result_t validation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    update_failure_t failure = UPDATE_FAILURE_NONE;
    int current_committed    = 0;
    int update_valid         = 0;

    if (memcmp(journal->running_manifest_sha256, journal->candidate_manifest_sha256, 32U) != 0)
    {
        set_error(journal, UPDATE_FAILURE_CURRENT_VERIFY);
        journal->state = UPDATE_STATE_FAILED;
        status         = UpdateJournal_Write(journal);
        return FirmwareStatus_IsError(status)
                   ? result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status)
                   : result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY,
                            FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    }

    if (journal->commit_attempts < UPDATE_MAX_COMMIT_ATTEMPTS)
        journal->commit_attempts++;
    status = UpdateJournal_Write(journal);
    if (FirmwareStatus_IsError(status))
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status);

    status = mount_storage();
    if (FirmwareStatus_IsError(status))
    {
        set_error(journal, UPDATE_FAILURE_STORAGE);
        (void) UpdateJournal_Write(journal);
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_STORAGE, status);
    }

    if (UpdateRequest_Load(&request) == UPDATE_REQUEST_ACTIVE)
        validation = PackageReader_ValidateRequest(UPDATE_PACKAGE_ROOT, &request,
                                                   journal->candidate_manifest_sha256, 1, &package);
    else
    {
        validation.failure = UPDATE_FAILURE_REQUEST_NOT_ACTIVE;
        validation.status  = FIRMWARE_STATUS_NOT_FOUND;
    }
    if (FirmwareStatus_IsOk(validation.status))
    {
        validation.status = PackageReader_ValidateUpdateRoot();
        if (FirmwareStatus_IsError(validation.status))
            validation.failure = UPDATE_FAILURE_FILE_SET;
    }
    if (FirmwareStatus_IsOk(validation.status))
    {
        update_valid = 1;
        status       = CurrentStore_Read(&current);
        if (FirmwareStatus_IsOk(status) &&
            memcmp(current.manifest_sha256, journal->candidate_manifest_sha256, 32U) == 0)
            current_committed = 1;
    }
    else
    {
        /* UPDATE may already have been deleted after a successful commit. */
        status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsOk(status) &&
            memcmp(current.manifest_sha256, journal->candidate_manifest_sha256, 32U) == 0)
            current_committed = 1;
        else
        {
            failure = validation.failure;
            status  = validation.status;
        }
    }
    if (current_committed != 0)
    {
        status = FIRMWARE_STATUS_OK;
    }
    else if (update_valid != 0)
    {
        LOG_INFO("update", "commit current start: package=%s", package.manifest.package_id);
        status = CurrentStore_Commit(&package, journal->candidate_manifest_sha256);
        if (FirmwareStatus_IsError(status))
            failure = UPDATE_FAILURE_CURRENT_COMMIT;
    }
    if (FirmwareStatus_IsOk(status))
        status = UpdateRequest_Delete();
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_SyncVolume();
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_CleanupUpdate();
    unmount_status = PlatformStorage_Unmount();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
    {
        status  = unmount_status;
        failure = UPDATE_FAILURE_STORAGE;
    }

    if (FirmwareStatus_IsError(status))
    {
        if (failure == UPDATE_FAILURE_NONE)
            failure = UPDATE_FAILURE_CURRENT_COMMIT;
        set_error(journal, failure);
        if (journal->commit_attempts >= UPDATE_MAX_COMMIT_ATTEMPTS)
        {
            journal->state = UPDATE_STATE_FAILED;
            (void) UpdateJournal_Write(journal);
            return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, failure, status);
        }
        (void) UpdateJournal_Write(journal);
        /* Keep both Pending and UPDATE so a later boot can retry the commit. */
        return result(UPDATE_OUTCOME_LAUNCH, failure, status);
    }

    pending_record        = *journal;
    pending_record.state  = UPDATE_STATE_IDLE;
    pending_record.source = UPDATE_SOURCE_CANDIDATE;
    pending_record.flags |= UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
    pending_record.last_error = UPDATE_FAILURE_CURRENT_COMMIT;
    journal->flags &= ~UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
    journal->state           = UPDATE_STATE_IDLE;
    journal->source          = UPDATE_SOURCE_NONE;
    journal->commit_attempts = 0U;
    clear_candidate_transaction(journal);
    status = UpdateJournal_Write(journal);
    if (FirmwareStatus_IsError(status))
    {
        /* CURRENT is valid; retain the old Pending record for a later retry. */
        (void) UpdateJournal_Write(&pending_record);
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_CURRENT_COMMIT, status);
    }
    LOG_INFO("update", "commit current complete");
    return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t handle_install_failure(update_journal_record_t *journal,
                                              update_failure_t failure, firmware_status_t status)
{
    set_error(journal, failure);
    if (journal->install_attempts < UPDATE_MAX_INSTALL_ATTEMPTS)
    {
        firmware_status_t journal_status = UpdateJournal_Write(journal);
        return FirmwareStatus_IsError(journal_status)
                   ? fail(journal, UPDATE_FAILURE_JOURNAL, journal_status, 1)
                   : result(UPDATE_OUTCOME_RESET, failure, status);
    }
    if (journal->source == UPDATE_SOURCE_CANDIDATE)
    {
        update_package_t current;
        firmware_status_t current_status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsError(current_status))
            return fail(journal, UPDATE_FAILURE_CURRENT_VERIFY, current_status, 1);
        journal->source           = UPDATE_SOURCE_ROLLBACK;
        journal->state            = UPDATE_STATE_INSTALLING;
        journal->install_attempts = 0U;
        journal->jump_attempts    = 0U;
        (void) memcpy(journal->running_manifest_sha256, current.manifest_sha256, 32U);
        LOG_WARN("update", "candidate install failed; rollback start");
        current_status = UpdateJournal_Write(journal);
        if (FirmwareStatus_IsError(current_status))
            return fail(journal, UPDATE_FAILURE_JOURNAL, current_status, 1);
        return process_installing(journal);
    }
    return fail(journal, UPDATE_FAILURE_ROLLBACK_INSTALL, status, 1);
}

static update_result_t install_package(update_journal_record_t *journal, const char *root,
                                       const uint8_t expected_digest[32])
{
    update_package_t package;
    update_operation_result_t operation;
    firmware_status_t status;
    size_t index;

    operation = PackageReader_Validate(root, expected_digest, 1, &package);
    if (FirmwareStatus_IsError(operation.status))
        return handle_install_failure(journal, operation.failure, operation.status);
    LOG_INFO("update", "upgrade content: package=%s version=%lu.%lu.%lu components=%lu source=%s",
             package.manifest.package_id, (unsigned long) package.manifest.release.major,
             (unsigned long) package.manifest.release.minor,
             (unsigned long) package.manifest.release.patch,
             (unsigned long) package.manifest.component_count, source_name(journal->source));
    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(package.manifest.components[index].name);
        size_t selected = index;
        size_t candidate;
        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        for (candidate = index + 1U; candidate < package.manifest.component_count; ++candidate)
            if (UpdateComponent_IsEnabled(
                    UpdateComponent_Find(package.manifest.components[candidate].name)) &&
                package.manifest.components[candidate].installation_order <
                    package.manifest.components[selected].installation_order)
                selected = candidate;
        if (selected != index)
        {
            update_manifest_component_t temporary = package.manifest.components[index];
            package.manifest.components[index]    = package.manifest.components[selected];
            package.manifest.components[selected] = temporary;
        }
        LOG_INFO("update", "install component[%lu/%lu]=%s order=%u size=%lu",
                 (unsigned long) (index + 1U), (unsigned long) package.manifest.component_count,
                 package.manifest.components[index].name,
                 (unsigned) package.manifest.components[index].installation_order,
                 (unsigned long) package.manifest.components[index].size);
        operation = ImageInstaller_Install(root, &package.manifest.components[index]);
        if (FirmwareStatus_IsError(operation.status))
            return handle_install_failure(journal, operation.failure, operation.status);
    }
    (void) memcpy(journal->running_manifest_sha256, package.manifest_sha256, 32U);
    journal->jump_attempts = 0U;
    journal->state         = UPDATE_STATE_JUMPING;
    status                 = UpdateJournal_Write(journal);
    return FirmwareStatus_IsError(status)
               ? fail(journal, UPDATE_FAILURE_JOURNAL, status, 1)
               : result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t process_installing(update_journal_record_t *journal)
{
    update_package_t current;
    firmware_status_t status;
    const char *root;
    const uint8_t *digest;

    if (journal->source == UPDATE_SOURCE_CANDIDATE)
    {
        root   = UPDATE_PACKAGE_ROOT;
        digest = journal->candidate_manifest_sha256;
    }
    else
    {
        root   = CURRENT_PACKAGE_ROOT;
        digest = journal->running_manifest_sha256;
    }

    if (journal->install_attempts >= UPDATE_MAX_INSTALL_ATTEMPTS)
    {
        if (journal->source == UPDATE_SOURCE_CANDIDATE)
        {
            status = CurrentStore_Read(&current);
            if (FirmwareStatus_IsError(status))
                return fail(journal, UPDATE_FAILURE_CURRENT_VERIFY, status, 1);
            journal->source           = UPDATE_SOURCE_ROLLBACK;
            journal->install_attempts = 0U;
            journal->jump_attempts    = 0U;
            (void) memcpy(journal->running_manifest_sha256, current.manifest_sha256, 32U);
            status = UpdateJournal_Write(journal);
            if (FirmwareStatus_IsError(status))
                return fail(journal, UPDATE_FAILURE_JOURNAL, status, 1);
            root   = CURRENT_PACKAGE_ROOT;
            digest = current.manifest_sha256;
            LOG_WARN("update", "candidate install retries exhausted; rollback start");
        }
        else
            return fail(journal, UPDATE_FAILURE_ROLLBACK_INSTALL, FIRMWARE_STATUS_IO_ERROR, 1);
    }

    journal->install_attempts++;
    status = UpdateJournal_Write(journal);
    if (FirmwareStatus_IsError(status))
        return fail(journal, UPDATE_FAILURE_JOURNAL, status, 1);
    return install_package(journal, root, digest);
}

static update_result_t process_jumping(update_journal_record_t *journal)
{
    firmware_status_t status;

    if (journal->jump_attempts >= UPDATE_MAX_JUMP_ATTEMPTS)
    {
        if (journal->source == UPDATE_SOURCE_CANDIDATE)
        {
            update_package_t current;
            status = mount_storage();
            if (FirmwareStatus_IsError(status))
                return fail(journal, UPDATE_FAILURE_STORAGE, status, 1);
            status = CurrentStore_Read(&current);
            if (FirmwareStatus_IsError(status))
            {
                (void) PlatformStorage_Unmount();
                return fail(journal, UPDATE_FAILURE_CURRENT_VERIFY, status, 1);
            }
            journal->source           = UPDATE_SOURCE_ROLLBACK;
            journal->state            = UPDATE_STATE_INSTALLING;
            journal->install_attempts = 0U;
            journal->jump_attempts    = 0U;
            (void) memcpy(journal->running_manifest_sha256, current.manifest_sha256, 32U);
            status = UpdateJournal_Write(journal);
            if (FirmwareStatus_IsError(status))
            {
                (void) PlatformStorage_Unmount();
                return fail(journal, UPDATE_FAILURE_JOURNAL, status, 1);
            }
            {
                LOG_WARN("update", "runtime launch failed; rollback start");
                update_result_t outcome = process_installing(journal);
                (void) PlatformStorage_Unmount();
                return outcome;
            }
        }
        return fail(journal, UPDATE_FAILURE_ROLLBACK_RUNTIME, FIRMWARE_STATUS_TIMEOUT, 1);
    }

    journal->jump_attempts++;
    status = UpdateJournal_Write(journal);
    return FirmwareStatus_IsError(status)
               ? fail(journal, UPDATE_FAILURE_JOURNAL, status, 1)
               : result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t UpdateService_ProcessJournalBoot(void)
{
    update_journal_record_t journal;
    update_package_t candidate;
    update_package_t current;
    firmware_status_t status;
    update_operation_result_t validation;
    update_result_t outcome;

    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
        LOG_INFO("update", "upgrade check: no request");
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }
    if (FirmwareStatus_IsError(status))
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status);

    if (journal.state == UPDATE_STATE_FAILED)
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, (update_failure_t) journal.last_error,
                      FIRMWARE_STATUS_INVALID_STATE);

    if (journal.state == UPDATE_STATE_IDLE && journal.source == UPDATE_SOURCE_NONE &&
        (journal.flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) == 0U)
        LOG_INFO("update", "upgrade check: no request");
    else
        LOG_INFO("update", "upgrade check: request found source=%s", source_name(journal.source));

    if (journal.state == UPDATE_STATE_IDLE &&
        (journal.flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) != 0U)
    {
        LOG_INFO("update", "commit current resume");
        return process_commit_pending(&journal);
    }

    if (journal.state == UPDATE_STATE_IDLE && journal.source == UPDATE_SOURCE_ROLLBACK)
    {
        status = mount_storage();
        if (FirmwareStatus_IsError(status))
        {
            set_error(&journal, UPDATE_FAILURE_STORAGE);
            (void) UpdateJournal_Write(&journal);
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_STORAGE, status);
        }
        status = UpdateRequest_Delete();
        if (FirmwareStatus_IsOk(status))
            status = PlatformStorage_SyncVolume();
        if (FirmwareStatus_IsOk(status))
            status = CurrentStore_CleanupUpdate();
        {
            firmware_status_t unmount_status = PlatformStorage_Unmount();
            if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
                status = unmount_status;
        }
        if (FirmwareStatus_IsError(status))
        {
            set_error(&journal, UPDATE_FAILURE_STORAGE);
            (void) UpdateJournal_Write(&journal);
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_STORAGE, status);
        }
        clear_candidate_transaction(&journal);
        journal.source = UPDATE_SOURCE_NONE;
        status         = UpdateJournal_Write(&journal);
        return FirmwareStatus_IsError(status)
                   ? result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_JOURNAL, status)
                   : result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }

    if (journal.state == UPDATE_STATE_REQUESTED)
    {
        update_request_t request;
        status = mount_storage();
        if (FirmwareStatus_IsError(status))
            return fail(&journal, UPDATE_FAILURE_STORAGE, status, 0);
        if (UpdateRequest_Load(&request) != UPDATE_REQUEST_ACTIVE)
        {
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_REQUEST_NOT_ACTIVE,
                          FIRMWARE_STATUS_NOT_FOUND);
        }
        if ((journal.component_mask != 0U && journal.component_mask != request.component_mask) ||
            (journal.candidate_package_id[0] != '\0' &&
             strcmp(journal.candidate_package_id, request.package_id) != 0))
        {
            (void) PlatformStorage_Unmount();
            return fail(&journal, UPDATE_FAILURE_REQUEST_COMPONENT_MASK_MISMATCH,
                        FIRMWARE_STATUS_AUTHENTICATION_FAILED, 0);
        }
        validation = PackageReader_ValidateRequest(
            UPDATE_PACKAGE_ROOT, &request, journal.candidate_manifest_sha256, 1, &candidate);
        if (FirmwareStatus_IsOk(validation.status))
        {
            validation.status = PackageReader_ValidateUpdateRoot();
            if (FirmwareStatus_IsError(validation.status))
                validation.failure = UPDATE_FAILURE_FILE_SET;
        }
        if (FirmwareStatus_IsError(validation.status))
        {
            (void) CurrentStore_CleanupUpdate();
            journal.state            = UPDATE_STATE_IDLE;
            journal.source           = UPDATE_SOURCE_NONE;
            journal.install_attempts = 0U;
            journal.jump_attempts    = 0U;
            set_error(&journal, validation.failure);
            {
                firmware_status_t write_status   = UpdateJournal_Write(&journal);
                firmware_status_t unmount_status = PlatformStorage_Unmount();
                if (FirmwareStatus_IsError(write_status))
                    return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                                  write_status);
                if (FirmwareStatus_IsError(unmount_status))
                    return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_STORAGE, unmount_status);
            }
            return result(UPDATE_OUTCOME_LAUNCH, validation.failure, validation.status);
        }
        journal.component_mask = request.component_mask;
        if (journal.candidate_package_id[0] == '\0')
            (void) strncpy(journal.candidate_package_id, request.package_id,
                           sizeof(journal.candidate_package_id) - 1U);
        status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsError(status))
        {
            (void) CurrentStore_CleanupUpdate();
            journal.state            = UPDATE_STATE_IDLE;
            journal.source           = UPDATE_SOURCE_NONE;
            journal.install_attempts = 0U;
            journal.jump_attempts    = 0U;
            set_error(&journal, UPDATE_FAILURE_CURRENT_VERIFY);
            (void) UpdateJournal_Write(&journal);
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_CURRENT_VERIFY, status);
        }
        if (VersionPolicy_Check(&candidate.manifest.release, &current.manifest.release, 1) !=
            UPDATE_VERSION_ALLOW)
        {
            (void) CurrentStore_CleanupUpdate();
            journal.state            = UPDATE_STATE_IDLE;
            journal.source           = UPDATE_SOURCE_NONE;
            journal.install_attempts = 0U;
            journal.jump_attempts    = 0U;
            set_error(&journal, UPDATE_FAILURE_VERSION);
            (void) UpdateJournal_Write(&journal);
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_VERSION,
                          FIRMWARE_STATUS_INVALID_STATE);
        }
        journal.state            = UPDATE_STATE_INSTALLING;
        journal.source           = UPDATE_SOURCE_CANDIDATE;
        journal.install_attempts = 0U;
        journal.jump_attempts    = 0U;
        status                   = UpdateJournal_Write(&journal);
        if (FirmwareStatus_IsError(status))
        {
            (void) PlatformStorage_Unmount();
            return fail(&journal, UPDATE_FAILURE_JOURNAL, status, 1);
        }
        outcome = process_installing(&journal);
        (void) PlatformStorage_Unmount();
        return outcome;
    }

    if (journal.state == UPDATE_STATE_INSTALLING)
    {
        status = mount_storage();
        if (FirmwareStatus_IsError(status))
            return fail(&journal, UPDATE_FAILURE_STORAGE, status, 1);
        outcome = process_installing(&journal);
        (void) PlatformStorage_Unmount();
        return outcome;
    }
    if (journal.state == UPDATE_STATE_JUMPING)
        return process_jumping(&journal);
    if (journal.state == UPDATE_STATE_IDLE)
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RECOVERY,
                  FIRMWARE_STATUS_INVALID_STATE);
}
#endif

#if (BOOTLOADER_UPDATE_USE_JOURNAL == 0U)
static update_result_t UpdateService_ProcessFileBoot(void)
{
    update_request_t request;
    update_package_t package;
    update_operation_result_t validation;
    update_result_t outcome;
    firmware_status_t status;
    update_request_presence_t request_presence;
    size_t index;

    /* 初始化文件系统 */
    status = mount_storage();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "direct update storage unavailable: status=%u", (unsigned) status);
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
    }
    /* 解析请求文件/UPDATE/boot_update_request.json */
    request_presence = UpdateRequest_Load(&request);
    LOG_INFO("update", "upgrade check: %s",
             request_presence == UPDATE_REQUEST_ACTIVE ? "request found" : "no request");
    switch (request_presence)
    {
        case UPDATE_REQUEST_ABSENT:
            /* 缺失升级请求，清空UPDATE文件夹 */
            status = CurrentStore_CleanupUpdate();
            if (FirmwareStatus_IsOk(status))
                status = PlatformStorage_SyncVolume();
            (void) PlatformStorage_Unmount();
            return FirmwareStatus_IsOk(status)
                       ? result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK)
                       : result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
        case UPDATE_REQUEST_INVALID:
            /* 升级请求无效 */
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_REQUEST_PARSE,
                          FIRMWARE_STATUS_INVALID_ARGUMENT);
        case UPDATE_REQUEST_ACTIVE:
            /* 升级请求有效！ */
            break;
        default:
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_REQUEST_PARSE,
                          FIRMWARE_STATUS_INVALID_STATE);
    }
    /* 验证Manifest.json和firmware是否存在 */
    validation.status = PackageReader_ValidateUpdateRoot();
    if (FirmwareStatus_IsOk(validation.status))
    {
        /* 验证Manifest.json与boot_update_request.json是否匹配 */
        validation =
            PackageReader_ValidateRequest(UPDATE_PACKAGE_ROOT, &request, NULL, 1, &package);
        if (FirmwareStatus_IsError(validation.status))
            validation.failure = UPDATE_FAILURE_FILE_SET;
    }
    if (FirmwareStatus_IsError(validation.status))
    {
        (void) PlatformStorage_Unmount();
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, validation.failure, validation.status);
    }
    LOG_INFO("update", "upgrade content: package=%s version=%lu.%lu.%lu components=%lu",
             package.manifest.package_id, (unsigned long) package.manifest.release.major,
             (unsigned long) package.manifest.release.minor,
             (unsigned long) package.manifest.release.patch,
             (unsigned long) package.manifest.component_count);
    /* 识别CURRENT历史版本是否倒置 */
    {
        update_package_t current;
        firmware_status_t current_status = CurrentStore_Read(&current);
        if (current_status != FIRMWARE_STATUS_NOT_FOUND && FirmwareStatus_IsError(current_status))
        {
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY,
                          current_status);
        }
        if (current_status == FIRMWARE_STATUS_OK &&
            VersionPolicy_Check(&package.manifest.release, &current.manifest.release, 1) !=
                UPDATE_VERSION_ALLOW)
        {
            (void) PlatformStorage_Unmount();
            return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_VERSION,
                          FIRMWARE_STATUS_INVALID_STATE);
        }
    }
    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(package.manifest.components[index].name);
        size_t selected = index;
        size_t candidate;
        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        for (candidate = index + 1U; candidate < package.manifest.component_count; ++candidate)
            if (UpdateComponent_IsEnabled(
                    UpdateComponent_Find(package.manifest.components[candidate].name)) &&
                package.manifest.components[candidate].installation_order <
                    package.manifest.components[selected].installation_order)
                selected = candidate;
        if (selected != index)
        {
            update_manifest_component_t temporary = package.manifest.components[index];
            package.manifest.components[index]    = package.manifest.components[selected];
            package.manifest.components[selected] = temporary;
        }
        {
            LOG_INFO("update", "install component[%lu/%lu]=%s order=%u size=%lu",
                     (unsigned long) (index + 1U), (unsigned long) package.manifest.component_count,
                     package.manifest.components[index].name,
                     (unsigned) package.manifest.components[index].installation_order,
                     (unsigned long) package.manifest.components[index].size);
            update_operation_result_t install =
                ImageInstaller_Install(UPDATE_PACKAGE_ROOT, &package.manifest.components[index]);
            if (FirmwareStatus_IsError(install.status))
            {
                (void) PlatformStorage_Unmount();
                return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, install.failure, install.status);
            }
        }
        PlatformSystem_WatchdogRefresh();
    }

    status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
    {
        (void) PlatformStorage_Unmount();
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);
    }
    LOG_INFO("update", "runtime verify pass; commit current start");
    status = CurrentStore_Commit(&package, package.manifest_sha256);
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_Read(&package);
    if (FirmwareStatus_IsOk(status))
        status = UpdateRequest_Delete();
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_SyncVolume();
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_CleanupUpdate();
    {
        firmware_status_t unmount_status = PlatformStorage_Unmount();
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
            status = unmount_status;
    }
    if (FirmwareStatus_IsError(status))
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_COMMIT, status);
    LOG_INFO("update", "commit current complete");
#if (BOOTLOADER_UPDATE_DEBUG_RESET_AFTER_COMMIT == 1U)
    outcome = result(UPDATE_OUTCOME_RESET, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
#else
    outcome = result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
#endif
    return outcome;
}
#endif

update_result_t UpdateService_Process(void)
{
    update_result_t outcome;
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
    outcome = UpdateService_ProcessJournalBoot();
#else
    outcome = UpdateService_ProcessFileBoot();
#endif
    LOG_INFO("update", "boot update flow complete: outcome=%u failure=%u status=%u",
             (unsigned) outcome.outcome, (unsigned) outcome.failure, (unsigned) outcome.status);
    return outcome;
}

firmware_status_t UpdateService_ConfirmRunning(const uint8_t running_manifest_sha256[32])
{
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 0U)
    (void) running_manifest_sha256;
    return FIRMWARE_STATUS_INVALID_STATE;
#else
    update_journal_record_t journal;
    update_source_t source;
    uint32_t expected_flags;
    firmware_status_t status = UpdateJournal_Read(&journal);

    LOG_INFO("update", "confirm running image: journal_status=%u", (unsigned) status);
    if (running_manifest_sha256 == NULL || FirmwareStatus_IsError(status) ||
        journal.state != UPDATE_STATE_JUMPING ||
        (journal.source != UPDATE_SOURCE_CANDIDATE && journal.source != UPDATE_SOURCE_ROLLBACK) ||
        memcmp(journal.running_manifest_sha256, running_manifest_sha256, 32U) != 0)
    {
        LOG_WARN("update", "running image confirmation rejected: state/source/digest mismatch");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    source                   = (update_source_t) journal.source;
    journal.state            = UPDATE_STATE_IDLE;
    journal.install_attempts = 0U;
    journal.jump_attempts    = 0U;
    if (journal.source == UPDATE_SOURCE_CANDIDATE)
    {
        journal.flags |= UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
        journal.commit_attempts = 0U;
    }
    else
        journal.flags &= ~UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
    journal.last_error = UPDATE_FAILURE_NONE;
    expected_flags     = journal.flags;
    status             = UpdateJournal_Write(&journal);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "running image confirmation journal write failed: status=%u",
                  (unsigned) status);
        return status;
    }
    status = UpdateJournal_Read(&journal);
    if (FirmwareStatus_IsError(status))
        return status;
    status = (journal.state == UPDATE_STATE_IDLE && journal.source == source &&
              journal.flags == expected_flags && journal.jump_attempts == 0U &&
              memcmp(journal.running_manifest_sha256, running_manifest_sha256, 32U) == 0)
                 ? FIRMWARE_STATUS_OK
                 : FIRMWARE_STATUS_IO_ERROR;
    LOG_INFO("update", "running image confirmation complete: status=%u commit_pending=%u",
             (unsigned) status,
             (unsigned) ((expected_flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) != 0U));
    return status;
#endif
}

update_result_t UpdateService_ReportRuntimeFailure(firmware_status_t status)
{
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 0U)
    return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);
#else
    update_journal_record_t journal;
    firmware_status_t journal_status;
    int rollback_started = 0;

    LOG_ERROR("update", "runtime reported failure: status=%u", (unsigned) status);
    journal_status = UpdateJournal_Read(&journal);
    if (FirmwareStatus_IsError(journal_status))
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, journal_status);
    if (journal.state != UPDATE_STATE_JUMPING)
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);
    set_error(&journal, UPDATE_FAILURE_RUNTIME_VECTOR);
    if (journal.source == UPDATE_SOURCE_CANDIDATE)
    {
        update_package_t current;
        journal_status = PlatformStorage_Init();
        if (FirmwareStatus_IsOk(journal_status))
            journal_status = PlatformStorage_Mount();
        if (FirmwareStatus_IsOk(journal_status))
            journal_status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsError(journal_status))
        {
            LOG_ERROR("update", "rollback unavailable; CURRENT verification failed: status=%u",
                      (unsigned) journal_status);
            journal.state = UPDATE_STATE_FAILED;
            set_error(&journal, UPDATE_FAILURE_CURRENT_VERIFY);
        }
        else
        {
            journal.source           = UPDATE_SOURCE_ROLLBACK;
            journal.state            = UPDATE_STATE_INSTALLING;
            journal.install_attempts = 0U;
            journal.jump_attempts    = 0U;
            (void) memcpy(journal.running_manifest_sha256, current.manifest_sha256, 32U);
            rollback_started = 1;
            LOG_WARN("update", "runtime failure accepted; rollback scheduled");
        }
        (void) PlatformStorage_Unmount();
    }
    else
    {
        journal.state = UPDATE_STATE_FAILED;
    }
    journal_status = UpdateJournal_Write(&journal);
    LOG_INFO("update", "runtime failure handling complete: rollback=%u journal_status=%u",
             (unsigned) rollback_started, (unsigned) journal_status);
    return FirmwareStatus_IsError(journal_status)
               ? result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, journal_status)
               : result(rollback_started != 0 ? UPDATE_OUTCOME_RESET
                                              : UPDATE_OUTCOME_RUNTIME_UNSAFE,
                        (update_failure_t) journal.last_error, status);
#endif
}

firmware_status_t UpdateService_SubmitCandidateEx(const char *package_id,
                                                  uint32_t candidate_version,
                                                  uint32_t component_mask,
                                                  const uint8_t manifest_sha256[32])
{
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 0U)
    (void) package_id;
    (void) candidate_version;
    (void) component_mask;
    (void) manifest_sha256;
    return FIRMWARE_STATUS_INVALID_STATE;
#else
    update_journal_record_t journal;
    firmware_status_t status;

    LOG_INFO("update", "submit candidate: package=%s version=%lu mask=0x%lx",
             package_id != NULL ? package_id : "(unspecified)", (unsigned long) candidate_version,
             (unsigned long) component_mask);
    if (manifest_sha256 == NULL || (component_mask & ~31U) != 0U ||
        (package_id != NULL && !UpdatePackage_IsValidId(package_id)))
    {
        LOG_WARN("update", "candidate rejected: invalid arguments");
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        (void) memset(&journal, 0, sizeof(journal));
    else if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "candidate rejected: journal read failed: status=%u",
                  (unsigned) status);
        return status;
    }
    else if (journal.state != UPDATE_STATE_IDLE ||
             (journal.flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) != 0U)
    {
        LOG_WARN("update", "candidate rejected: update busy (state=%s flags=0x%lx)",
                 state_name(journal.state), (unsigned long) journal.flags);
        return FIRMWARE_STATUS_BUSY;
    }

    journal.state             = UPDATE_STATE_REQUESTED;
    journal.source            = UPDATE_SOURCE_CANDIDATE;
    journal.flags             = 0U;
    journal.install_attempts  = 0U;
    journal.jump_attempts     = 0U;
    journal.commit_attempts   = 0U;
    journal.last_error        = UPDATE_FAILURE_NONE;
    journal.candidate_version = candidate_version;
    journal.component_mask    = component_mask;
    (void) memset(journal.candidate_package_id, 0, sizeof(journal.candidate_package_id));
    if (package_id != NULL)
        (void) strncpy(journal.candidate_package_id, package_id,
                       sizeof(journal.candidate_package_id) - 1U);
    (void) memcpy(journal.candidate_manifest_sha256, manifest_sha256, 32U);
    (void) memset(journal.running_manifest_sha256, 0, sizeof(journal.running_manifest_sha256));
    status = UpdateJournal_Write(&journal);
    LOG_INFO("update", "candidate submission %s: status=%u",
             FirmwareStatus_IsOk(status) ? "accepted" : "failed", (unsigned) status);
    return status;
#endif
}

firmware_status_t UpdateService_SubmitCandidate(uint32_t candidate_version,
                                                const uint8_t manifest_sha256[32])
{
    return UpdateService_SubmitCandidateEx(NULL, candidate_version, 0U, manifest_sha256);
}
