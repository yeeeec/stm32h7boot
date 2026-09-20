#ifndef FIRMWARE_UPDATE_SERVICE_H
#define FIRMWARE_UPDATE_SERVICE_H

#include <stdint.h>

#include "firmware/update_journal.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    update_result_t UpdateService_Process(void);
    update_result_t UpdateService_ReportRuntimeFailure(firmware_status_t status);
    firmware_status_t UpdateService_SubmitCandidate(uint32_t candidate_version,
                                                    const uint8_t manifest_sha256[32]);
    firmware_status_t UpdateService_ConfirmRunning(const uint8_t running_manifest_sha256[32]);

#ifdef __cplusplus
}
#endif

#endif
