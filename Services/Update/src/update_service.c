#include "update/update_service.h"

#include <string.h>

#include "bootloader_config.h"
#include "current_store.h"
#include "image_installer.h"
#include "logging.h"
#include "platform/platform_storage.h"
#include "runtime_verifier.h"
#include "update_config.h"
#include "update_journal_internal.h"
#include "update_package.h"

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
#include "platform/platform_journal_storage.h"
#endif

static uint8_t s_update_initialized;

static firmware_status_t mount_update_storage(void)
{
    firmware_status_t status = PlatformStorage_Init();
    return FirmwareStatus_IsError(status) ? status : PlatformStorage_Mount();
}

static firmware_status_t unmount_update_storage(void)
{
    return PlatformStorage_Unmount();
}

static firmware_status_t install_all_components(const char *root, update_package_t *package)
{
    size_t index;
    firmware_status_t status;

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
        status = ImageInstaller_Install(root, &package->manifest.components[index]);
        if (FirmwareStatus_IsError(status))
            return status;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t execute_validated_package(const char *source_root,
                                                   update_package_t *package)
{
    firmware_status_t status = install_all_components(source_root, package);
    if (FirmwareStatus_IsError(status))
        return status;
    status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
        return status;
    status = CurrentStore_RebuildFrom(source_root);
    if (FirmwareStatus_IsError(status))
        return status;
    return CurrentStore_Verify();
}

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 0U)
static firmware_status_t execute_transaction(update_target_t target)
{
    const char *source_root =
        target == UPDATE_TARGET_UPDATE ? UPDATE_PACKAGE_ROOT : LAST_PACKAGE_ROOT;
    update_package_t package;
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;

    if (FirmwareStatus_IsError(status))
        return status;

    status = PackageReader_Validate(source_root, &package);
    if (FirmwareStatus_IsOk(status))
        status = execute_validated_package(source_root, &package);

    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    return status;
}

static firmware_status_t recover_current_runtime(void)
{
    update_package_t current_package;
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;
    int runtime_validated = 0;

    if (FirmwareStatus_IsError(status))
        goto failed;

    status = CurrentStore_Read(&current_package);
    if (FirmwareStatus_IsError(status))
        goto unmount_failed;

    status = RuntimeVerifier_VerifyPackage(&current_package);
    if (status == FIRMWARE_STATUS_AUTHENTICATION_FAILED)
    {
        status = install_all_components(CURRENT_PACKAGE_ROOT, &current_package);
        if (FirmwareStatus_IsError(status))
            goto unmount_failed;
        status = RuntimeVerifier_Validate();
        if (FirmwareStatus_IsOk(status))
        {
            runtime_validated = 1;
            status = RuntimeVerifier_VerifyPackage(&current_package);
        }
    }
    if (FirmwareStatus_IsError(status))
        goto unmount_failed;

    if (runtime_validated == 0)
        status = RuntimeVerifier_Validate();
    if (FirmwareStatus_IsError(status))
        goto unmount_failed;

    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsError(unmount_status))
    {
        status = unmount_status;
        goto failed;
    }
    status = UpdateJournal_ResetIdle();
    if (FirmwareStatus_IsError(status))
        LOG_ERROR("update", "Journal recovery reset failed: status=%u", (unsigned) status);
    return status;

unmount_failed:
    (void) unmount_update_storage();
failed:
    LOG_ERROR("update", "CURRENT recovery failed: status=%u", (unsigned) status);
    return status;
}

static firmware_status_t prepare_update(void)
{
    update_package_t update_package;
    update_package_t current_package;
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;

    if (FirmwareStatus_IsError(status))
        goto failed;

    status = PackageReader_Validate(UPDATE_PACKAGE_ROOT, &update_package);
    if (FirmwareStatus_IsError(status))
        goto cleanup;

    status = CurrentStore_Read(&current_package);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
        status = FIRMWARE_STATUS_OK;
        goto cleanup;
    }
    if (FirmwareStatus_IsError(status))
        goto cleanup;

    if (UpdateVersion_Compare(&update_package.manifest.release,
                              &current_package.manifest.release) < 0)
    {
        LOG_ERROR("update", "downgrade rejected");
        status = FIRMWARE_STATUS_INVALID_STATE;
        goto cleanup;
    }

    status = CurrentStore_SaveLast();
    if (FirmwareStatus_IsError(status))
        goto cleanup;
    status = CurrentStore_VerifyLast();

cleanup:
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
failed:
    if (FirmwareStatus_IsError(status))
        LOG_ERROR("update", "upgrade preparation failed: status=%u", (unsigned) status);
    return status;
}

static firmware_status_t prepare_rollback(void)
{
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;

    if (FirmwareStatus_IsError(status))
        goto failed;

    status = CurrentStore_VerifyLast();
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        LOG_ERROR("update", "rollback LAST missing");
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
failed:
    if (FirmwareStatus_IsError(status) && status != FIRMWARE_STATUS_NOT_FOUND)
        LOG_ERROR("update", "rollback preparation failed: status=%u", (unsigned) status);
    return status;
}
#endif

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
static int debug_package_status_is_ignorable(firmware_status_t status)
{
    switch (status)
    {
        case FIRMWARE_STATUS_NOT_FOUND:
        case FIRMWARE_STATUS_INVALID_ARGUMENT:
        case FIRMWARE_STATUS_INVALID_STATE:
        case FIRMWARE_STATUS_NOT_SUPPORTED:
        case FIRMWARE_STATUS_OUT_OF_RANGE:
        case FIRMWARE_STATUS_BUFFER_TOO_SMALL:
        case FIRMWARE_STATUS_AUTHENTICATION_FAILED:
            return 1;
        default:
            return 0;
    }
}

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

static firmware_status_t process_debug_update(void)
{
    update_package_t package;
    firmware_status_t status = mount_update_storage();
    firmware_status_t unmount_status;

    if (FirmwareStatus_IsError(status))
        return status;

    status = PackageReader_Validate(UPDATE_PACKAGE_ROOT, &package);
    if (FirmwareStatus_IsError(status))
    {
        firmware_status_t unmount_status = unmount_update_storage();
        LOG_INFO("update", "debug UPDATE ignored: status=%u", (unsigned) status);
        if (!debug_package_status_is_ignorable(status))
            return status;
        return FirmwareStatus_IsError(unmount_status) ? unmount_status : FIRMWARE_STATUS_OK;
    }

    status = execute_validated_package(UPDATE_PACKAGE_ROOT, &package);
    unmount_status = unmount_update_storage();
    if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(unmount_status))
        status = unmount_status;
    if (FirmwareStatus_IsError(status))
        return status;

    cleanup_debug_update();
    return FIRMWARE_STATUS_OK;
}
#else
static firmware_status_t finish_transaction(update_target_t target)
{
    firmware_status_t status;

    if (target == UPDATE_TARGET_ROLLBACK)
    {
        status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "Journal rollback completion failed: status=%u",
                      (unsigned) status);
            return status;
        }
        LOG_INFO("update", "rollback installed: journal=IDLE/NONE");
    }
    else
    {
        status = UpdateJournal_WriteState(UPDATE_STATE_JUMPING, UPDATE_TARGET_UPDATE);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "Journal upgrade completion failed: status=%u",
                      (unsigned) status);
            return status;
        }
        LOG_INFO("update", "upgrade installed: journal=JUMPING/UPDATE");
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t process_pending(update_target_t target)
{
    firmware_status_t status = target == UPDATE_TARGET_UPDATE ? prepare_update() : prepare_rollback();
    if (FirmwareStatus_IsError(status))
        return status;

    status = UpdateJournal_WriteState(UPDATE_STATE_WRITING, target);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "Journal WRITING update failed: status=%u", (unsigned) status);
        return status;
    }
    status = execute_transaction(target);
    if (FirmwareStatus_IsError(status))
        return status;
    return finish_transaction(target);
}

static firmware_status_t process_production_update(void)
{
    update_journal_record_t journal;
    firmware_status_t status = UpdateJournal_Read(&journal);

    if (status == FIRMWARE_STATUS_NOT_FOUND || status == FIRMWARE_STATUS_INVALID_STATE)
        return recover_current_runtime();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "Journal read failed: status=%u", (unsigned) status);
        return status;
    }

    LOG_INFO("update", "process journal: state=%lu target=%lu sequence=%lu",
             (unsigned long) journal.state, (unsigned long) journal.target,
             (unsigned long) journal.sequence);

    if (journal.state == UPDATE_STATE_IDLE && journal.target == UPDATE_TARGET_NONE)
        return FIRMWARE_STATUS_OK;

    if (journal.state == UPDATE_STATE_PENDING)
    {
        status = process_pending((update_target_t) journal.target);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "pending transaction failed: target=%u status=%u",
                      (unsigned) journal.target, (unsigned) status);
            return status;
        }
        return FIRMWARE_STATUS_OK;
    }
    if (journal.state == UPDATE_STATE_WRITING)
    {
        status = execute_transaction((update_target_t) journal.target);
        if (FirmwareStatus_IsOk(status))
            status = finish_transaction((update_target_t) journal.target);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "interrupted transaction failed: target=%u status=%u",
                      (unsigned) journal.target, (unsigned) status);
            return status;
        }
        return FIRMWARE_STATUS_OK;
    }
    if (journal.state == UPDATE_STATE_JUMPING && journal.target == UPDATE_TARGET_UPDATE)
    {
        status = execute_transaction(UPDATE_TARGET_ROLLBACK);
        if (FirmwareStatus_IsOk(status))
            status = UpdateJournal_WriteState(UPDATE_STATE_IDLE, UPDATE_TARGET_NONE);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "JUMPING recovery failed: status=%u", (unsigned) status);
            return status;
        }
        return FIRMWARE_STATUS_OK;
    }

    LOG_ERROR("update", "Journal state unsupported: state=%lu target=%lu",
              (unsigned long) journal.state, (unsigned long) journal.target);
    return FIRMWARE_STATUS_INVALID_STATE;
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

firmware_status_t UpdateService_Process(void)
{
    if (s_update_initialized == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;

#if (BOOTLOADER_UPDATE_DEBUG_MODE == 1U)
    return process_debug_update();
#else
    return process_production_update();
#endif
}
