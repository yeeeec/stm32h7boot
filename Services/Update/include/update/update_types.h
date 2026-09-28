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
        IMAGE_TARGET_CONFIG
    } image_target_t;

#ifdef __cplusplus
}
#endif

#endif
