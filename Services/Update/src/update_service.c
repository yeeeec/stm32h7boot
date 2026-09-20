#include "update/update_service.h"

#include <string.h>

#include "platform/platform_storage.h"
#include "update_config.h"
#include "update_internal.h"

static update_result_t result(update_outcome_t outcome, update_failure_t failure,
                              firmware_status_t status)
{
    update_result_t value = {outcome, failure, status};
    return value;
}

static firmware_status_t mount_storage(void)
{
    firmware_status_t status = PlatformStorage_Init();
    return FirmwareStatus_IsOk(status) ? PlatformStorage_Mount() : status;
}

static void set_error(update_journal_record_t *journal, update_failure_t failure)
{
    journal->last_error = (uint32_t) failure;
}

static void clear_candidate_transaction(update_journal_record_t *journal)
{
    journal->candidate_version = 0U;
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
    update_package_t package;
    update_package_t current;
    update_journal_record_t pending_record;
    update_operation_result_t validation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    update_failure_t failure = UPDATE_FAILURE_NONE;
    int current_committed    = 0;
    int update_valid         = 0;

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

    validation = PackageReader_Validate(UPDATE_PACKAGE_ROOT, journal->candidate_manifest_sha256, 1,
                                        &package);
    if (FirmwareStatus_IsOk(validation.status))
    {
        update_valid = 1;
        status       = CurrentStore_Read(&current);
        if (FirmwareStatus_IsOk(status) &&
            memcmp(current.raw_manifest_sha256, journal->candidate_manifest_sha256, 32U) == 0)
            current_committed = 1;
    }
    else
    {
        /* UPDATE may already have been deleted after a successful commit. */
        status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsOk(status) &&
            memcmp(current.raw_manifest_sha256, journal->candidate_manifest_sha256, 32U) == 0)
            current_committed = 1;
        else
        {
            failure = validation.failure;
            status  = validation.status;
        }
    }
    if (current_committed != 0)
    {
        status = CurrentStore_CleanupUpdate();
        if (FirmwareStatus_IsError(status))
            failure = UPDATE_FAILURE_CURRENT_COMMIT;
    }
    else if (update_valid != 0)
    {
        status = CurrentStore_Commit(&package, journal->candidate_manifest_sha256);
        if (FirmwareStatus_IsError(status))
            failure = UPDATE_FAILURE_CURRENT_COMMIT;
        if (FirmwareStatus_IsOk(status))
        {
            status = CurrentStore_CleanupUpdate();
            if (FirmwareStatus_IsError(status))
                failure = UPDATE_FAILURE_CURRENT_COMMIT;
        }
    }
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
        (void) UpdateJournal_Write(journal);
        /* Keep both Pending and UPDATE so a later boot can retry the commit. */
        return result(UPDATE_OUTCOME_LAUNCH, failure, status);
    }

    pending_record = *journal;
    journal->flags &= ~UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
    journal->state  = UPDATE_STATE_IDLE;
    journal->source = UPDATE_SOURCE_CANDIDATE;
    pending_record  = *journal;
    pending_record.flags |= UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING;
    pending_record.last_error = UPDATE_FAILURE_CURRENT_COMMIT;
    journal->commit_attempts  = 0U;
    clear_candidate_transaction(journal);
    status = UpdateJournal_Write(journal);
    if (FirmwareStatus_IsError(status))
    {
        /* CURRENT is valid; retain the old Pending record for a later retry. */
        (void) UpdateJournal_Write(&pending_record);
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_CURRENT_COMMIT, status);
    }
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
        (void) memcpy(journal->running_manifest_sha256, current.raw_manifest_sha256, 32U);
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
    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        operation = ImageInstaller_Install(root, &package.manifest.components[index]);
        if (FirmwareStatus_IsError(operation.status))
            return handle_install_failure(journal, operation.failure, operation.status);
    }
    (void) memcpy(journal->running_manifest_sha256, package.raw_manifest_sha256, 32U);
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
            (void) memcpy(journal->running_manifest_sha256, current.raw_manifest_sha256, 32U);
            status = UpdateJournal_Write(journal);
            if (FirmwareStatus_IsError(status))
                return fail(journal, UPDATE_FAILURE_JOURNAL, status, 1);
            root   = CURRENT_PACKAGE_ROOT;
            digest = current.raw_manifest_sha256;
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
            (void) memcpy(journal->running_manifest_sha256, current.raw_manifest_sha256, 32U);
            status = UpdateJournal_Write(journal);
            if (FirmwareStatus_IsError(status))
            {
                (void) PlatformStorage_Unmount();
                return fail(journal, UPDATE_FAILURE_JOURNAL, status, 1);
            }
            {
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

update_result_t UpdateService_Process(void)
{
    update_journal_record_t journal;
    update_package_t candidate;
    update_package_t current;
    firmware_status_t status;
    update_operation_result_t validation;
    update_result_t outcome;

    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    if (FirmwareStatus_IsError(status))
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status);

    if (journal.state == UPDATE_STATE_FAILED)
        return result(UPDATE_OUTCOME_RUNTIME_UNSAFE, (update_failure_t) journal.last_error,
                      FIRMWARE_STATUS_INVALID_STATE);

    if (journal.state == UPDATE_STATE_IDLE &&
        (journal.flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) != 0U)
        return process_commit_pending(&journal);

    if (journal.state == UPDATE_STATE_IDLE && journal.source == UPDATE_SOURCE_ROLLBACK)
    {
        status = mount_storage();
        if (FirmwareStatus_IsError(status))
        {
            set_error(&journal, UPDATE_FAILURE_STORAGE);
            (void) UpdateJournal_Write(&journal);
            return result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_STORAGE, status);
        }
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
        journal.source = UPDATE_SOURCE_CANDIDATE;
        status         = UpdateJournal_Write(&journal);
        return FirmwareStatus_IsError(status)
                   ? result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_JOURNAL, status)
                   : result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }

    if (journal.state == UPDATE_STATE_REQUESTED)
    {
        status = mount_storage();
        if (FirmwareStatus_IsError(status))
            return fail(&journal, UPDATE_FAILURE_STORAGE, status, 0);
        validation = PackageReader_Validate(UPDATE_PACKAGE_ROOT, journal.candidate_manifest_sha256,
                                            0, &candidate);
        if (FirmwareStatus_IsError(validation.status))
        {
            (void) CurrentStore_CleanupUpdate();
            journal.state            = UPDATE_STATE_IDLE;
            journal.source           = UPDATE_SOURCE_CANDIDATE;
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
        status = CurrentStore_Read(&current);
        if (FirmwareStatus_IsError(status))
        {
            (void) CurrentStore_CleanupUpdate();
            journal.state            = UPDATE_STATE_IDLE;
            journal.source           = UPDATE_SOURCE_CANDIDATE;
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
            journal.source           = UPDATE_SOURCE_CANDIDATE;
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

firmware_status_t UpdateService_ConfirmRunning(const uint8_t running_manifest_sha256[32])
{
    update_journal_record_t journal;
    firmware_status_t status = UpdateJournal_Read(&journal);

    if (running_manifest_sha256 == NULL || FirmwareStatus_IsError(status) ||
        journal.state != UPDATE_STATE_JUMPING ||
        (journal.source != UPDATE_SOURCE_CANDIDATE && journal.source != UPDATE_SOURCE_ROLLBACK) ||
        memcmp(journal.running_manifest_sha256, running_manifest_sha256, 32U) != 0)
        return FIRMWARE_STATUS_INVALID_STATE;
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
    status = UpdateJournal_Write(&journal);
    return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_OK;
}

update_result_t UpdateService_ReportRuntimeFailure(firmware_status_t status)
{
    update_journal_record_t journal;
    firmware_status_t journal_status;
    int rollback_started = 0;

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
            journal.state = UPDATE_STATE_FAILED;
            set_error(&journal, UPDATE_FAILURE_CURRENT_VERIFY);
        }
        else
        {
            journal.source           = UPDATE_SOURCE_ROLLBACK;
            journal.state            = UPDATE_STATE_INSTALLING;
            journal.install_attempts = 0U;
            journal.jump_attempts    = 0U;
            (void) memcpy(journal.running_manifest_sha256, current.raw_manifest_sha256, 32U);
            rollback_started = 1;
        }
        (void) PlatformStorage_Unmount();
    }
    else
    {
        journal.state = UPDATE_STATE_FAILED;
    }
    journal_status = UpdateJournal_Write(&journal);
    return FirmwareStatus_IsError(journal_status)
               ? result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, journal_status)
               : result(rollback_started != 0 ? UPDATE_OUTCOME_RESET
                                              : UPDATE_OUTCOME_RUNTIME_UNSAFE,
                        (update_failure_t) journal.last_error, status);
}

firmware_status_t UpdateService_SubmitCandidate(uint32_t candidate_version,
                                                const uint8_t manifest_sha256[32])
{
    update_journal_record_t journal;
    firmware_status_t status;

    if (manifest_sha256 == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        (void) memset(&journal, 0, sizeof(journal));
    else if (FirmwareStatus_IsError(status))
        return status;
    else if (journal.state != UPDATE_STATE_IDLE ||
             (journal.flags & UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) != 0U)
        return FIRMWARE_STATUS_BUSY;

    journal.state             = UPDATE_STATE_REQUESTED;
    journal.source            = UPDATE_SOURCE_CANDIDATE;
    journal.flags             = 0U;
    journal.install_attempts  = 0U;
    journal.jump_attempts     = 0U;
    journal.commit_attempts   = 0U;
    journal.last_error        = UPDATE_FAILURE_NONE;
    journal.candidate_version = candidate_version;
    (void) memcpy(journal.candidate_manifest_sha256, manifest_sha256, 32U);
    (void) memset(journal.running_manifest_sha256, 0, sizeof(journal.running_manifest_sha256));
    return UpdateJournal_Write(&journal);
}
