#include "update/update_manager.h"

#include <string.h>

#include "firmware/boot_config.h"
#include "update/update_request.h"
#include "update/version_policy.h"

firmware_status_t UpdateManager_Execute(const storage_boot_update_request_t *request,
                                        const update_manifest_t *manifest,
                                        const char *actual_manifest_hash,
                                        const update_version_t *current_version, int current_exists,
                                        const update_manager_port_t *port, int *runtime_modified)
{
    firmware_status_t status;
    if (request == NULL || manifest == NULL || actual_manifest_hash == NULL || port == NULL ||
        port->install == NULL || port->commit == NULL || runtime_modified == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = UpdateRequest_Validate(request);
    if (FirmwareStatus_IsError(status) || strcmp(request->package_id, manifest->package_id) != 0 ||
        strcmp(request->manifest_sha256, actual_manifest_hash) != 0)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    {
        uint32_t manifest_mask = 0U;
        size_t i;
        for (i = 0U; i < manifest->component_count; ++i)
            manifest_mask |= manifest->components[i].mask;
        if (manifest_mask != request->component_mask)
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = UpdateManifest_ValidateTarget(manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    {
        const update_version_t running = {BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR,
                                          BOOTLOADER_VERSION_PATCH, 0U};
        status = VersionPolicy_ValidateMinimumBootloader(&manifest->minimum_bootloader_version,
                                                         &running);
        if (FirmwareStatus_IsError(status))
            return status;
    }
    status = VersionPolicy_Check(&manifest->release, current_version, current_exists) ==
                     UPDATE_VERSION_ALLOW
                 ? FIRMWARE_STATUS_OK
                 : FIRMWARE_STATUS_INVALID_STATE;
    if (FirmwareStatus_IsError(status))
        return status;
    *runtime_modified = 0;
    status            = port->install(port->context, manifest, runtime_modified);
    if (FirmwareStatus_IsError(status))
        return status;
    return port->commit(port->context, manifest);
}
