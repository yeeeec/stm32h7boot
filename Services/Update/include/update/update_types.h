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
        IMAGE_TARGET_THERAPY,
        IMAGE_TARGET_VOICE,
        IMAGE_TARGET_CONFIG,
        IMAGE_TARGET_RESOURCE
    } image_target_t;

    typedef enum
    {
        UPDATE_OUTCOME_RUNTIME_UNSAFE = 0,
        UPDATE_OUTCOME_LAUNCH
    } update_outcome_t;

    typedef struct
    {
        update_outcome_t outcome;
        firmware_status_t status;
    } update_result_t;

#ifdef __cplusplus
}
#endif

#endif
