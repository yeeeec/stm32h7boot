#include "update/recovery_manager.h"

#include <stddef.h>

firmware_status_t RecoveryManager_Run(const recovery_manager_t *manager)
{
    unsigned attempt, retries;
    int runtime_modified     = 0;
    firmware_status_t status = FIRMWARE_STATUS_INVALID_STATE;
    if (manager == NULL || manager->restore == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    retries = manager->max_retries == 0U ? 1U : manager->max_retries + 1U;
    for (attempt = 0U; attempt < retries; ++attempt)
    {
        runtime_modified = 1;
        status           = manager->restore(manager->context, &runtime_modified);
        if (FirmwareStatus_IsOk(status))
            return FIRMWARE_STATUS_OK;
    }
    return status;
}
