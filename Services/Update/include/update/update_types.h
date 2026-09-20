#ifndef FIRMWARE_UPDATE_TYPES_H
#define FIRMWARE_UPDATE_TYPES_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        IMAGE_TARGET_APP = 0,
        IMAGE_TARGET_GUI,
        IMAGE_TARGET_THERAPY
    } image_target_t;

    typedef enum
    {
        UPDATE_OUTCOME_NO_CHANGE = 0,
        UPDATE_OUTCOME_INSTALLED,
        UPDATE_OUTCOME_RECOVERED,
        UPDATE_OUTCOME_RUNTIME_UNSAFE
    } update_outcome_t;

    typedef enum
    {
        UPDATE_FAILURE_NONE = 0,
        UPDATE_FAILURE_JOURNAL,
        UPDATE_FAILURE_STORAGE,
        UPDATE_FAILURE_MANIFEST_READ,
        UPDATE_FAILURE_MANIFEST_DIGEST,
        UPDATE_FAILURE_MANIFEST_PARSE,
        UPDATE_FAILURE_SIGNATURE,
        UPDATE_FAILURE_TARGET,
        UPDATE_FAILURE_VERSION,
        UPDATE_FAILURE_FILE_SET,
        UPDATE_FAILURE_INSTALL_SOURCE,
        UPDATE_FAILURE_INSTALL_SIZE,
        UPDATE_FAILURE_INSTALL_ERASE,
        UPDATE_FAILURE_INSTALL_WRITE,
        UPDATE_FAILURE_INSTALL_READBACK,
        UPDATE_FAILURE_INSTALL_HASH,
        UPDATE_FAILURE_INSTALL_CLOSE,
        UPDATE_FAILURE_THERAPY_EXIT,
        UPDATE_FAILURE_CURRENT_VERIFY,
        UPDATE_FAILURE_CURRENT_COMMIT,
        UPDATE_FAILURE_CURRENT_RECONCILE,
        UPDATE_FAILURE_RECOVERY
    } update_failure_t;

    typedef struct
    {
        update_outcome_t outcome;
        update_failure_t failure;
        firmware_status_t status;
    } update_result_t;

#ifdef __cplusplus
}
#endif

#endif
