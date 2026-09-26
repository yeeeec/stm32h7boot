#ifndef FIRMWARE_UPDATE_SERVICE_H
#define FIRMWARE_UPDATE_SERVICE_H

#include "firmware/status.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t UpdateService_Init(void);
    update_result_t UpdateService_Process(void);

#ifdef __cplusplus
}
#endif

#endif
