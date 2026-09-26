#include "update/update_service.h"

#include <string.h>

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

static update_result_t install_update(void)
{
    update_request_t request;
    update_package_t package;
    update_operation_result_t operation;
    firmware_status_t status;
    firmware_status_t unmount_status;
    size_t index;

    status = mount_update_storage();
    if (FirmwareStatus_IsError(status))
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);

    if (UpdateRequest_Load(&request) != UPDATE_REQUEST_ACTIVE)
    {
        (void)unmount_update_storage();
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_REQUEST_NOT_ACTIVE,
                           FIRMWARE_STATUS_NOT_FOUND);
    }

    operation = PackageReader_ValidateRequest(UPDATE_PACKAGE_ROOT, &request, NULL, 1, &package);
    if (FirmwareStatus_IsError(operation.status))
    {
        (void)unmount_update_storage();
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, operation.failure, operation.status);
    }

    /* Installation order is part of the existing manifest/package contract. */
    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        size_t selected = index;
        size_t candidate;
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(package.manifest.components[index].name);
        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        for (candidate = index + 1U; candidate < package.manifest.component_count; ++candidate)
        {
            const update_component_descriptor_t *candidate_descriptor =
                UpdateComponent_Find(package.manifest.components[candidate].name);
            if (candidate_descriptor != NULL && UpdateComponent_IsEnabled(candidate_descriptor) &&
                package.manifest.components[candidate].installation_order <
                    package.manifest.components[selected].installation_order)
                selected = candidate;
        }
        if (selected != index)
        {
            update_manifest_component_t temporary = package.manifest.components[index];
            package.manifest.components[index] = package.manifest.components[selected];
            package.manifest.components[selected] = temporary;
        }

        operation = ImageInstaller_Install(UPDATE_PACKAGE_ROOT,
                                            &package.manifest.components[index]);
        if (FirmwareStatus_IsError(operation.status))
        {
            (void)unmount_update_storage();
            return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, operation.failure,
                               operation.status);
        }
    }

    status = CurrentStore_Commit(&package, package.manifest_sha256);
    if (FirmwareStatus_IsOk(status))
        status = UpdateRequest_Delete();
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_SyncVolume();
    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_CleanupUpdate();
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    if (FirmwareStatus_IsError(status))
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_STORAGE, status);
    return make_result(UPDATE_OUTCOME_INSTALLED, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static firmware_status_t validate_pending_update(void)
{
    update_request_t request;
    update_package_t package;
    update_operation_result_t operation;
    firmware_status_t status = mount_update_storage();

    if (FirmwareStatus_IsError(status))
        return status;
    if (UpdateRequest_Load(&request) != UPDATE_REQUEST_ACTIVE)
    {
        (void)unmount_update_storage();
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    operation = PackageReader_ValidateRequest(UPDATE_PACKAGE_ROOT, &request, NULL, 1, &package);
    status = operation.status;
    if (FirmwareStatus_IsOk(status))
        status = PackageReader_ValidateUpdateRoot();
    {
        firmware_status_t unmount_status = unmount_update_storage();
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
            status = unmount_status;
    }
    return status;
}

static int update_request_available(void)
{
    update_request_t request;
    firmware_status_t status = mount_update_storage();
    int available = 0;
    if (FirmwareStatus_IsOk(status))
    {
        available = UpdateRequest_Load(&request) == UPDATE_REQUEST_ACTIVE;
        (void)unmount_update_storage();
    }
    return available;
}

static update_result_t finish_install(void)
{
    firmware_status_t status;

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
    status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    LOG_INFO("update", "debug update confirmed: journal=IDLE/NONE");
#else
    status = UpdateJournal_WriteState(UPDATE_STATE_JUMPING, UPDATE_TARGET_UPDATE);
    if (FirmwareStatus_IsError(status))
        return journal_error(status);
    LOG_INFO("update", "update installed: journal=JUMPING/UPDATE");
#endif
    return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_result_t process_update(update_state_t state)
{
    firmware_status_t status;
    update_result_t result;

    if (state == UPDATE_STATE_PENDING)
    {
        status = validate_pending_update();
        if (FirmwareStatus_IsError(status))
            return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_MANIFEST_READ,
                               status);
        /* Persist WRITING before the first operation that changes firmware. */
        status = UpdateJournal_WriteState(UPDATE_STATE_WRITING, UPDATE_TARGET_UPDATE);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
    }

    result = install_update();
    if (result.outcome != UPDATE_OUTCOME_INSTALLED)
    {
        /* WRITING/UPDATE is deliberately retained for the next boot. */
        LOG_ERROR("update", "update execution failed: failure=%u status=%u",
                  (unsigned)result.failure, (unsigned)result.status);
        return result;
    }
    return finish_install();
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
             state_name((update_state_t)journal.state), (unsigned long)journal.target,
             (unsigned long)journal.sequence);

    if (journal.state == UPDATE_STATE_IDLE && journal.target == UPDATE_TARGET_NONE)
    {
        /* The request file is the input edge; the Journal remains the only
         * transaction state source after this transition. */
        if (update_request_available())
        {
            status = UpdateJournal_WriteState(UPDATE_STATE_PENDING, UPDATE_TARGET_UPDATE);
            if (FirmwareStatus_IsError(status))
                return journal_error(status);
            return process_update(UPDATE_STATE_PENDING);
        }
        return make_result(UPDATE_OUTCOME_LAUNCH, UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    }
    if (journal.target == UPDATE_TARGET_ROLLBACK)
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_ROLLBACK_UNSUPPORTED,
                           FIRMWARE_STATUS_NOT_SUPPORTED);
    if (journal.target != UPDATE_TARGET_UPDATE)
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                           FIRMWARE_STATUS_INVALID_STATE);

    if (journal.state == UPDATE_STATE_PENDING)
        return process_update(UPDATE_STATE_PENDING);
    if (journal.state == UPDATE_STATE_WRITING)
        return process_update(UPDATE_STATE_WRITING);
    if (journal.state == UPDATE_STATE_JUMPING)
    {
#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
        return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                           FIRMWARE_STATUS_INVALID_STATE);
#else
        status = UpdateJournal_WriteState(UPDATE_STATE_WRITING, UPDATE_TARGET_UPDATE);
        if (FirmwareStatus_IsError(status))
            return journal_error(status);
        return process_update(UPDATE_STATE_WRITING);
#endif
    }
    return make_result(UPDATE_OUTCOME_RUNTIME_UNSAFE, UPDATE_FAILURE_JOURNAL,
                       FIRMWARE_STATUS_INVALID_STATE);
}
