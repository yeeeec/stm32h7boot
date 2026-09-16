#ifndef FIRMWARE_UPDATE_MANAGER_H
#define FIRMWARE_UPDATE_MANAGER_H

#include "firmware/status.h"
#include "ports/storage_types.h"
#include "update/update_manifest.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef firmware_status_t (*update_install_package_fn)(void *context,
                                                           const update_manifest_t *manifest,
                                                           int *runtime_modified);
    typedef firmware_status_t (*update_commit_package_fn)(void *context,
                                                          const update_manifest_t *manifest);

    typedef struct
    {
        update_install_package_fn install;
        update_commit_package_fn commit;
        void *context;
    } update_manager_port_t;

    firmware_status_t UpdateManager_Execute(const storage_boot_update_request_t *request,
                                            const update_manifest_t *manifest,
                                            const char *actual_manifest_hash,
                                            const update_version_t *current_version,
                                            int current_exists, const update_manager_port_t *port,
                                            int *runtime_modified);

#ifdef __cplusplus
}
#endif

#endif
