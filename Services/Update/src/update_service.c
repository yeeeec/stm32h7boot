#include "update/update_service.h"

#include <stddef.h>

#include "platform/platform_storage.h"
#include "update_config.h"
#include "update_internal.h"

static update_result_t service_result(update_outcome_t outcome, update_failure_t failure,
                                      firmware_status_t status)
{
    update_result_t result = {outcome, failure, status};
    return result;
}

static firmware_status_t storage_mount(void)
{
    firmware_status_t status = PlatformStorage_Init();
    return FirmwareStatus_IsOk(status) ? PlatformStorage_Mount() : status;
}

static update_result_t recover_mounted(update_failure_t original_failure,
                                       firmware_status_t original_status)
{
    firmware_status_t status = CurrentStore_Reconcile();

    if (FirmwareStatus_IsOk(status))
        status = CurrentStore_Restore();
    if (FirmwareStatus_IsOk(status))
        status = UpdateJournal_Clear();
    if (FirmwareStatus_IsError(status))
        return service_result(UPDATE_OUTCOME_RUNTIME_UNSAFE,
                              UPDATE_FAILURE_RECOVERY, status);
    return service_result(UPDATE_OUTCOME_RECOVERED, original_failure,
                          original_status);
}

update_result_t UpdateService_RecoverInterrupted(void)
{
    update_journal_record_t journal;
    update_result_t result;
    firmware_status_t status = UpdateJournal_Read(&journal);

    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return service_result(UPDATE_OUTCOME_NO_CHANGE, UPDATE_FAILURE_NONE,
                              FIRMWARE_STATUS_OK);
    if (FirmwareStatus_IsError(status))
        return service_result(UPDATE_OUTCOME_RUNTIME_UNSAFE,
                              UPDATE_FAILURE_JOURNAL, status);

    status = storage_mount();
    if (FirmwareStatus_IsError(status))
        return service_result(UPDATE_OUTCOME_RUNTIME_UNSAFE,
                              UPDATE_FAILURE_STORAGE, status);
    result = recover_mounted(UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
    status = PlatformStorage_Unmount();
    if (FirmwareStatus_IsError(status) && result.outcome == UPDATE_OUTCOME_RECOVERED)
    {
        result.failure = UPDATE_FAILURE_STORAGE;
        result.status = status;
    }
    return result;
}

update_result_t UpdateService_Install(const BootRequestMessage_t *request)
{
    update_package_t package;
    update_package_t current;
    update_operation_result_t operation;
    update_result_t result;
    firmware_status_t status;
    size_t index;
    int current_exists = 0;

    if (request == NULL || request->request != BOOT_REQUEST_UPDATE ||
        request->format_version != BOOT_REQUEST_FORMAT_VERSION)
        return service_result(UPDATE_OUTCOME_NO_CHANGE,
                              UPDATE_FAILURE_MANIFEST_DIGEST,
                              FIRMWARE_STATUS_INVALID_ARGUMENT);

    status = storage_mount();
    if (FirmwareStatus_IsError(status))
        return service_result(UPDATE_OUTCOME_NO_CHANGE, UPDATE_FAILURE_STORAGE,
                              status);

    status = CurrentStore_Reconcile();
    if (status != FIRMWARE_STATUS_OK && status != FIRMWARE_STATUS_NOT_FOUND)
    {
        result = service_result(UPDATE_OUTCOME_NO_CHANGE,
                                UPDATE_FAILURE_CURRENT_RECONCILE, status);
        goto unmount;
    }

    operation = PackageReader_Validate(UPDATE_PACKAGE_ROOT,
                                       request->manifest_sha256, 0, &package);
    if (FirmwareStatus_IsError(operation.status))
    {
        result = service_result(UPDATE_OUTCOME_NO_CHANGE, operation.failure,
                                operation.status);
        goto unmount;
    }

    status = CurrentStore_Read(&current);
    if (status == FIRMWARE_STATUS_OK)
        current_exists = 1;
    else if (status != FIRMWARE_STATUS_NOT_FOUND)
    {
        result = service_result(UPDATE_OUTCOME_NO_CHANGE,
                                UPDATE_FAILURE_CURRENT_VERIFY, status);
        goto unmount;
    }
    if (VersionPolicy_Check(&package.manifest.release,
                            current_exists != 0 ? &current.manifest.release : NULL,
                            current_exists) != UPDATE_VERSION_ALLOW)
    {
        result = service_result(UPDATE_OUTCOME_NO_CHANGE, UPDATE_FAILURE_VERSION,
                                FIRMWARE_STATUS_INVALID_STATE);
        goto unmount;
    }

    status = UpdateJournal_Write(UPDATE_PHASE_INSTALLING,
                                 package.raw_manifest_sha256);
    if (FirmwareStatus_IsError(status))
    {
        result = service_result(UPDATE_OUTCOME_NO_CHANGE, UPDATE_FAILURE_JOURNAL,
                                status);
        goto unmount;
    }

    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        operation = ImageInstaller_Install(
            package.root, &package.manifest.components[index]);
        if (FirmwareStatus_IsError(operation.status))
        {
            result = recover_mounted(operation.failure, operation.status);
            goto unmount;
        }
    }

    status = UpdateJournal_Write(UPDATE_PHASE_COMMITTING,
                                 package.raw_manifest_sha256);
    if (FirmwareStatus_IsError(status))
    {
        result = recover_mounted(UPDATE_FAILURE_JOURNAL, status);
        goto unmount;
    }
    status = CurrentStore_Commit(&package);
    if (FirmwareStatus_IsError(status))
    {
        result = recover_mounted(UPDATE_FAILURE_CURRENT_COMMIT, status);
        goto unmount;
    }
    status = UpdateJournal_Clear();
    if (FirmwareStatus_IsError(status))
    {
        result = recover_mounted(UPDATE_FAILURE_JOURNAL, status);
        goto unmount;
    }
    result = service_result(UPDATE_OUTCOME_INSTALLED, UPDATE_FAILURE_NONE,
                            FIRMWARE_STATUS_OK);

unmount:
    status = PlatformStorage_Unmount();
    if (FirmwareStatus_IsError(status) &&
        (result.outcome == UPDATE_OUTCOME_INSTALLED ||
         result.outcome == UPDATE_OUTCOME_RECOVERED))
    {
        result.failure = UPDATE_FAILURE_STORAGE;
        result.status = status;
    }
    return result;
}
