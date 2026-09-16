#include "update/version_policy.h"
#include "update/update_manifest.h"

update_version_decision_t VersionPolicy_Check(const update_version_t *update,
                                              const update_version_t *current, int current_exists)
{
    if (update == 0 || current_exists < 0 || current_exists > 1)
        return UPDATE_VERSION_REJECT;
    if (current_exists == 0)
        return UPDATE_VERSION_ALLOW;
    if (current == 0)
        return UPDATE_VERSION_REJECT;
    return UpdateVersion_Compare(update, current) >= 0 ? UPDATE_VERSION_ALLOW
                                                       : UPDATE_VERSION_REJECT;
}

firmware_status_t VersionPolicy_ValidateMinimumBootloader(const update_version_t *required,
                                                          const update_version_t *running)
{
    if (required == 0 || running == 0)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return UpdateVersion_Compare(running, required) >= 0 ? FIRMWARE_STATUS_OK
                                                         : FIRMWARE_STATUS_INVALID_STATE;
}
