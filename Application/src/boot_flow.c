#include "boot_flow.h"

#include <stddef.h>

#include "boot/runtime_image.h"
#include "logging.h"
#include "update/update_service.h"

boot_flow_result_t BootFlow_Run(uint32_t *vector_address)
{
    update_result_t result;

    if (vector_address == NULL)
    {
        LOG_ERROR("boot", "boot flow cannot start: vector address is null");
        return BOOT_FLOW_FATAL;
    }

    LOG_INFO("boot", "startup update check");
    result = UpdateService_Process();
    if (result.outcome == UPDATE_OUTCOME_RUNTIME_UNSAFE)
    {
        LOG_ERROR("boot", "update flow failed: failure=%u status=%u", (unsigned) result.failure,
                  (unsigned) result.status);
        // return BOOT_FLOW_FATAL;
    }
    if (result.outcome == UPDATE_OUTCOME_RESET)
        return BOOT_FLOW_RESET;

    LOG_INFO("boot", "runtime image check start");
    firmware_status_t status = RuntimeImage_Prepare(vector_address);
    if (FirmwareStatus_IsError(status) && result.outcome == UPDATE_OUTCOME_LAUNCH)
    {
        update_result_t runtime_failure = UpdateService_ReportRuntimeFailure(status);
        if (runtime_failure.outcome == UPDATE_OUTCOME_RESET)
            return BOOT_FLOW_RESET;
        return BOOT_FLOW_FATAL;
    }
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "runtime image check failed: status=%u", (unsigned) status);
        return BOOT_FLOW_FATAL;
    }
    LOG_INFO("boot", "runtime image ready: vector=0x%08lx", (unsigned long) *vector_address);
    return BOOT_FLOW_LAUNCH;
}
