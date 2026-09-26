#include "update/update_service.h"

#include "bootloader_config.h"
#include "logging.h"
#include "platform/platform_journal_storage.h"
#include "platform/platform_storage.h"
#include "update_config.h"
#include "update_internal.h"

static uint8_t s_update_initialized;

static update_result_t make_result(update_outcome_t outcome, update_failure_t failure,
                                   firmware_status_t status)
{
    update_result_t value = {outcome, failure, status};
    return value;
}

static update_result_t journal_error(firmware_status_t status)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL, status);
}

static const char *state_name(update_state_t state)
{
    switch (state)
    {
        case UPDATE_STATE_IDLE: return "IDLE";
        case UPDATE_STATE_PENDING: return "PENDING";
        case UPDATE_STATE_WRITING: return "WRITING";
        case UPDATE_STATE_JUMPING: return "JUMPING";
        default: return "invalid";
    }
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

static update_result_t package_failure(const update_operation_result_t *operation)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, operation->failure, operation->status);
}

static update_result_t storage_failure(firmware_status_t status)
{
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
}

static update_operation_result_t install_all_components(const char *root, update_package_t *package)
{
    update_operation_result_t result = {UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK};
    size_t index;

    /* Sort a local manifest copy by the existing installation order contract. */
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

static update_result_t execute_transaction(update_target_t target)
{
    const char *source_root = target == UPDATE_TARGET_UPDATE ? UPDATE_PACKAGE_ROOT : LAST_PACKAGE_ROOT;
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
    {
        result = package_failure(&operation);
        goto cleanup;
    }
    operation = install_all_components(source_root, &package);
    if (FirmwareStatus_IsError(operation.status))
    {
        result = package_failure(&operation);
        goto cleanup;
    }
    status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
    {
        result = make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_RUNTIME_VECTOR, status);
        goto cleanup;
    }
    status = CurrentStore_RebuildFrom(source_root);
    if (FirmwareStatus_IsError(status))
    {
        result = make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY, status);
        goto cleanup;
    }
    status = CurrentStore_Verify();
    if (FirmwareStatus_IsError(status))
    {
        result = make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_CURRENT_VERIFY, status);
        goto cleanup;
    }
    result = make_result(UPDATE_OUTCOME_INSTALLED, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);

cleanup:
    unmount_status = unmount_update_storage();
    if (result.outcome == UPDATE_OUTCOME_INSTALLED && FirmwareStatus_IsError(unmount_status))
        result = storage_failure(unmount_status);
    return result;
}

static firmware_status_t prepare_update(void)
{
    update_package_t update_package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return status;
    operation = PackageReader_Validate(UPDATE_PACKAGE_ROOT, NULL, 1, &update_package);
    status = operation.status;
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_Verify();
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_SaveLast();
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_VerifyLast();
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    return status;
}

static firmware_status_t prepare_rollback(void)
{
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;

    if (FirmwareStatus_IsError(status))
        return status;
    status = CurrentStore_VerifyLast();
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    return status;
}

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
#endif

static update_result_t finish_transaction(update_target_t target)
{
    firmware_status_t status;

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
    status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    LOG_INFO("update", "debug transaction confirmed: target=%u journal=IDLE/NONE",
             (unsigned) target);
    if (target == UPDATE_TARGET_UPDATE)
        cleanup_debug_update();
#else
    status = UpdateJournal_WriteState(UPDATE_STATE_JUMPING, target);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    LOG_INFO("update", "transaction installed: journal=JUMPING/%u", (unsigned) target);
#endif
    return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t process_pending(update_target_t target)
{
    firmware_status_t status;
    update_result_t result;

    status = target == UPDATE_TARGET_UPDATE ? prepare_update() : prepare_rollback();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "transaction preparation failed: target=%u status=%u",
                  (unsigned) target, (unsigned) status);
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
    }
    status = UpdateJournal_WriteState(UPDATE_STATE_WRITING, target);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    result = execute_transaction(target);
    if (result.outcome != UPDATE_OUTCOME_INSTALLED)
    {
        LOG_ERROR("update", "transaction execution failed: target=%u failure=%u status=%u",
                  (unsigned) target, (unsigned) result.failure, (unsigned) result.status);
        return result;
    }
    return finish_transaction(target);
}

firmware_status_t UpdateService_Init(void)
{
    update_journal_record_t journal;
    firmware_status_t status;

    if (s_update_initialized != 0U)
        return FIRMWARE_STATUS_OK;
    status = PlatformJournalStorage_Init();
    if (FirmwareStatus_IsError(status))
        return status;

    status = UpdateJournal_Read(&journal);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
        status = UpdateJournal_WriteState(UPDATE_STATE_PENDING, UPDATE_TARGET_UPDATE);
#else
        status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
#endif
    }
    if (FirmwareStatus_IsError(status))
        return status;
    s_update_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

update_result_t UpdateService_Process(void)
{
    update_journal_record_t journal;
    firmware_status_t status;
    update_result_t result;

    if (s_update_initialized == 0U)
    {
        status = UpdateService_Init();
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
    }
    status = UpdateJournal_Read(&journal);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);

    LOG_INFO("update", "process journal: state=%s target=%lu sequence=%lu",
             state_name((update_state_t) journal.state), (unsigned long) journal.target,
             (unsigned long) journal.sequence);

    if (journal.state == UPDATE_STATE_IDLE && journal.target == UPDATE_TARGET_NONE)
        return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    if (journal.target != UPDATE_TARGET_UPDATE && journal.target != UPDATE_TARGET_ROLLBACK)
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                           FIRMWARE_STATUS_INVALID_STATE);

    if (journal.state == UPDATE_STATE_PENDING)
        return process_pending((update_target_t) journal.target);
    if (journal.state == UPDATE_STATE_WRITING)
    {
        result = execute_transaction((update_target_t) journal.target);
        if (result.outcome != UPDATE_OUTCOME_INSTALLED)
            return result;
        return finish_transaction((update_target_t) journal.target);
    }
    if (journal.state == UPDATE_STATE_JUMPING)
    {
#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                           FIRMWARE_STATUS_INVALID_STATE);
#else
        status = UpdateJournal_WriteState(UPDATE_STATE_WRITING,
                                          (update_target_t) journal.target);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
        result = execute_transaction((update_target_t) journal.target);
        if (result.outcome != UPDATE_OUTCOME_INSTALLED)
            return result;
        return finish_transaction((update_target_t) journal.target);
#endif
    }
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                       FIRMWARE_STATUS_INVALID_STATE);
}
