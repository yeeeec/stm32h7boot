#ifndef FIRMWARE_RECOVERY_MANAGER_H
#define FIRMWARE_RECOVERY_MANAGER_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef firmware_status_t (*recovery_restore_fn)(void *context, int *runtime_modified);

    typedef struct
    {
        recovery_restore_fn restore;
        void *context;
        unsigned max_retries;
    } recovery_manager_t;

    /* Recovery intentionally does not call VersionPolicy. */
    firmware_status_t RecoveryManager_Run(const recovery_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif
