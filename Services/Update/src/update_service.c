#include "update/update_service.h"

#include "bootloader_config.h"
#include "logging.h"
#include "platform/platform_storage.h"
#include "update_config.h"
#include "update_internal.h"

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
#include "platform/platform_journal_storage.h"
#endif

static uint8_t s_update_initialized;

static update_result_t make_result(update_outcome_t outcome, update_failure_t failure,
                                   firmware_status_t status)
{
    update_result_t value = {outcome, failure, status};
    return value;
}

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
static update_result_t journal_error(firmware_status_t status)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status);
}
#endif

static update_result_t storage_failure(firmware_status_t status)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
}

static update_result_t package_failure(const update_operation_result_t *operation)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, operation->failure, operation->status);
}

static firmware_status_t mount_update_storage(void)
{
    firmware_status_t status = PlatformStorage_Init();
    return FirmwareStatus_IsError(status) ? status : PlatformStorage_Mount();
}

static firmware_status_t unmount_update_storage(void)
{
    return PlatformStorage_Unmount();
}

static update_operation_result_t install_all_components(const char *root, update_package_t *package)
{
    update_operation_result_t result = {UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK};
    size_t index;

    /* Preserve the manifest's existing installation order contract. */
    for (index = 0U; index < package->manifest.component_count; ++index)
    {
        size_t selected = index;
        size_t candidate;
        for (candidate = index + 1U; candidate < package->manifest.component_count; ++candidate)
        {
            if (package->manifest.components[candidate].installation_order <
                package->manifest.components[selected].installation_order)
                selected = candidate;
        }
        if (selected != index)
        {
            update_manifest_component_t temporary = package->manifest.components[index];
            package->manifest.components[index] = package->manifest.components[selected];
            package->manifest.components[selected] = temporary;
        }
    }

    for (index = 0U; index < package->manifest.component_count; ++index)
    {
        result = ImageInstaller_Install(root, &package->manifest.components[index]);
        if (FirmwareStatus_IsError(result.status))
            return result;
    }
    return result;
}

/* The package has already passed manifest, file-set, and payload validation. */
static update_result_t execute_validated_package(const char *source_root,
                                                 update_package_t *package)
{
    update_operation_result_t operation;
    firmware_status_t status;

    operation = install_all_components(source_root, package);
    if (FirmwareStatus_IsError(operation.status))
        return package_failure(&operation);

    status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);

    status = CurrentStore_RebuildFrom(source_root);
    if (FirmwareStatus_IsError(status))
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY, status);

    status = CurrentStore_Verify();
    if (FirmwareStatus_IsError(status))
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY, status);

    return make_result(UPDATE_OUTCOME_INSTALLED, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
static update_result_t execute_transaction(update_target_t target)
{
    const char *source_root =
        target == UPDATE_TARGET_UPDATE ? UPDATE_PACKAGE_ROOT : LAST_PACKAGE_ROOT;
    update_package_t package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    update_result_t result;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return storage_failure(status);

    operation = PackageReader_Validate(source_root, NULL, 1, &package);
    if (FirmwareStatus_IsError(operation.status))
        result = package_failure(&operation);
    else
        result = execute_validated_package(source_root, &package);

    unmount_status = unmount_update_storage();
    if (result.outcome == UPDATE_OUTCOME_INSTALLED && FirmwareStatus_IsError(unmount_status))
        result = storage_failure(unmount_status);
    return result;
}

static update_result_t recover_current_runtime(void)
{
    update_package_t current_package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    int runtime_validated = 0;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return storage_failure(status);

    status = CurrentStore_Read(&current_package);
    if (FirmwareStatus_IsError(status))
    {
        (void) unmount_update_storage();
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY, status);
    }

    status = RuntimeVerifier_VerifyPackage(&current_package);
    if (status == FIRMWARE_STATUS_AUTHENTICATION_FAILED)
    {
        operation = install_all_components(CURRENT_PACKAGE_ROOT, &current_package);
        if (FirmwareStatus_IsError(operation.status))
        {
            (void) unmount_update_storage();
            return package_failure(&operation);
        }
        status = RuntimeVerifier_Validate();
        if (FirmwareStatus_IsOk(status))
        {
            runtime_validated = 1;
            status = RuntimeVerifier_VerifyPackage(&current_package);
        }
    }
    if (FirmwareStatus_IsError(status))
    {
        (void) unmount_update_storage();
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE,
                           UPDATE_FAILURE_CURRENT_VERIFY,
                           status);
    }

    if (runtime_validated == 0)
        status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
    {
        (void) unmount_update_storage();
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);
    }
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsError(unmount_status))
        return storage_failure(unmount_status);

    status = UpdateJournal_ResetIdle();
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static firmware_status_t prepare_update(update_failure_t *failure)
{
    update_package_t update_package;
    update_package_t current_package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;

    if (failure == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *failure = UPDATE_FAILURE_STORAGE;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return status;

    operation = PackageReader_Validate(UPDATE_PACKAGE_ROOT, NULL, 1, &update_package);
    status = operation.status;
    if (FirmwareStatus_IsError(status))
    {
        *failure = operation.failure;
        goto cleanup;
    }

    status = CurrentStore_Read(&current_package);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
        /* A missing CURRENT directory is the only first-install case. */
        status = FIRMWARE_STATUS_OK;
        goto cleanup;
    }
    if (FirmwareStatus_IsError(status))
    {
        *failure = UPDATE_FAILURE_CURRENT_VERIFY;
        goto cleanup;
    }

    if (VersionPolicy_Check(&update_package.manifest.release, &current_package.manifest.release,
                            1) != UPDATE_VERSION_ALLOW)
    {
        *failure = UPDATE_FAILURE_VERSION;
        status = FIRMWARE_STATUS_INVALID_STATE;
        goto cleanup;
    }

    status = CurrentStore_SaveLast();
    if (FirmwareStatus_IsError(status))
    {
        *failure = UPDATE_FAILURE_CURRENT_VERIFY;
        goto cleanup;
    }
    status = CurrentStore_VerifyLast();
    if (FirmwareStatus_IsError(status))
        *failure = UPDATE_FAILURE_CURRENT_VERIFY;

cleanup:
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    return status;
}

static firmware_status_t prepare_rollback(update_failure_t *failure)
{
    firmware_status_t status;
    firmware_status_t unmount_status;

    if (failure == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *failure = UPDATE_FAILURE_ROLLBACK_UNSUPPORTED;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return status;

    status = CurrentStore_VerifyLast();
    if (status != FIRMWARE_STATUS_OK)
        *failure = status == FIRMWARE_STATUS_NOT_FOUND ? UPDATE_FAILURE_ROLLBACK_UNSUPPORTED
                                                        : UPDATE_FAILURE_CURRENT_VERIFY;

    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    return status;
}
#endif

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
static void cleanup_debug_update(void)
{
    firmware_status_t status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "debug UPDATE cleanup skipped: status=%u", (unsigned) status);
        return;
    }
    status = CurrentStore_CleanupUpdate();
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_SyncVolume();
    {
        firmware_status_t unmount_status = unmount_update_storage();
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
            status = unmount_status;
    }
    if (FirmwareStatus_IsError(status))
        LOG_ERROR("update", "debug UPDATE cleanup failed: status=%u", (unsigned) status);
}

static update_result_t process_debug_update(void)
{
    update_package_t package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    update_result_t result;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return storage_failure(status);

    operation = PackageReader_Validate(UPDATE_PACKAGE_ROOT, NULL, 1, &package);
    if (FirmwareStatus_IsError(operation.status))
    {
        /* An absent or invalid debug package is simply not an update request. */
        LOG_INFO("update", "debug UPDATE ignored: failure=%u status=%u",
                 (unsigned) operation.failure, (unsigned) operation.status);
        (void) unmount_update_storage();
        return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }

    result = execute_validated_package(UPDATE_PACKAGE_ROOT, &package);
    unmount_status = unmount_update_storage();
    if (result.outcome == UPDATE_OUTCOME_INSTALLED && FirmwareStatus_IsError(unmount_status))
        return storage_failure(unmount_status);
    if (result.outcome != UPDATE_OUTCOME_INSTALLED)
        return result;

    cleanup_debug_update();
    return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}
#else
static update_result_t finish_transaction(update_target_t target)
{
    firmware_status_t status;

    if (target == UPDATE_TARGET_ROLLBACK)
    {
        status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
        LOG_INFO("update", "rollback installed: journal=IDLE/NONE");
    }
    else
    {
        status = UpdateJournal_WriteState(UPDATE_STATE_JUMPING, UPDATE_TARGET_UPDATE);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
        LOG_INFO("update", "upgrade installed: journal=JUMPING/UPDATE");
    }
    return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t process_pending(update_target_t target)
{
    firmware_status_t status;
    update_result_t result;
    update_failure_t failure = UPDATE_FAILURE_STORAGE;

    status = target == UPDATE_TARGET_UPDATE ? prepare_update(&failure) : prepare_rollback(&failure);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "transaction preparation failed: target=%u status=%u",
                  (unsigned) target, (unsigned) status);
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, failure, status);
    }
    status = UpdateJournal_WriteState(UPDATE_STATE_WRITING, target);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    result = execute_transaction(target);
    if (result.outcome != UPDATE_OUTCOME_INSTALLED)
        return result;
    return finish_transaction(target);
}

static update_result_t process_production_update(void)
{
    update_journal_record_t journal;
    firmware_status_t status;
    update_result_t result;

    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND || status == FIRMWARE_STATUS_INVALID_STATE)
        return recover_current_runtime();
    if (FirmwareStatus_IsError(status))
        return journal_error(status);

    LOG_INFO("update", "process journal: state=%lu target=%lu sequence=%lu",
             (unsigned long) journal.state, (unsigned long) journal.target,
             (unsigned long) journal.sequence);

    if (journal.state == UPDATE_STATE_IDLE && journal.target == UPDATE_TARGET_NONE)
        return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    if (journal.state == UPDATE_STATE_PENDING)
        return process_pending((update_target_t) journal.target);
    if (journal.state == UPDATE_STATE_WRITING)
    {
        result = execute_transaction((update_target_t) journal.target);
        if (result.outcome != UPDATE_OUTCOME_INSTALLED)
            return result;
        return finish_transaction((update_target_t) journal.target);
    }
    if (journal.state == UPDATE_STATE_JUMPING && journal.target == UPDATE_TARGET_UPDATE)
    {
        /* Recover LAST and keep JUMPING/UPDATE on every recovery failure. */
        result = execute_transaction(UPDATE_TARGET_ROLLBACK);
        if (result.outcome != UPDATE_OUTCOME_INSTALLED)
            return result;
        status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
        return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }
    return journal_error(FIRMWARE_STATUS_INVALID_STATE);
}
#endif

firmware_status_t UpdateService_Init(void)
{
#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
    firmware_status_t status;
#endif

    if (s_update_initialized != 0U)
        return FIRMWARE_STATUS_OK;

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
    status = PlatformJournalStorage_Init();
    if (FirmwareStatus_IsError(status))
        return status;
#endif

    s_update_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

update_result_t UpdateService_Process(void)
{
    firmware_status_t status;

    if (s_update_initialized == 0U)
    {
        status = UpdateService_Init();
        if (FirmwareStatus_IsError(status))
#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
            return storage_failure(status);
#else
            return journal_error(status);
#endif
    }

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
    return process_debug_update();
#else
    return process_production_update();
#endif
}
