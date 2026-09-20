#ifndef FIRMWARE_UPDATE_SERVICE_H
#define FIRMWARE_UPDATE_SERVICE_H

#include "firmware/boot_request.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    update_result_t UpdateService_RecoverInterrupted(void);
    update_result_t UpdateService_Install(const BootRequestMessage_t *request);

#ifdef __cplusplus
}
#endif

#endif
