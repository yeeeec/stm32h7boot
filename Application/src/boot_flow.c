#include "boot_flow.h"

#include <stddef.h>

#include "boot/runtime_image.h"
#include "logging.h"
#include "update/update_service.h"

boot_flow_result_t BootFlow_Run(uint32_t *vector_address)
{
    update_result_t result;

    if (vector_address == NULL)
        return BOOT_FLOW_FATAL;

    LOG_DEBUG("bootloader", "processing update journal");
    result = UpdateService_Process();
    if (result.outcome == UPDATE_OUTCOME_RUNTIME_UNSAFE)
    {
        LOG_ERROR("bootloader", "recovery failed: stage=%u status=%u", (unsigned) result.failure,
                  (unsigned) result.status);
        return BOOT_FLOW_FATAL;
    }
    if (result.outcome == UPDATE_OUTCOME_RESET)
        return BOOT_FLOW_RESET;

    firmware_status_t status = RuntimeImage_Prepare(vector_address);
    if (FirmwareStatus_IsError(status) && result.outcome == UPDATE_OUTCOME_LAUNCH)
    {
        update_result_t runtime_failure = UpdateService_ReportRuntimeFailure(status);
        if (runtime_failure.outcome == UPDATE_OUTCOME_RESET)
            return BOOT_FLOW_RESET;
        return BOOT_FLOW_FATAL;
    }
    return FirmwareStatus_IsOk(status) ? BOOT_FLOW_LAUNCH : BOOT_FLOW_FATAL;
}
