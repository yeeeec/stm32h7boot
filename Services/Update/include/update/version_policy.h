#ifndef FIRMWARE_VERSION_POLICY_H
#define FIRMWARE_VERSION_POLICY_H

#include "firmware/status.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        UPDATE_VERSION_REJECT = 0,
        UPDATE_VERSION_ALLOW  = 1
    } update_version_decision_t;

    /* UPDATE >= CURRENT is accepted; a missing CURRENT is accepted as well. */
    update_version_decision_t VersionPolicy_Check(const update_version_t *update,
                                                  const update_version_t *current,
                                                  int current_exists);

    firmware_status_t VersionPolicy_ValidateMinimumBootloader(const update_version_t *required,
                                                              const update_version_t *running);

#ifdef __cplusplus
}
#endif

#endif
