#include "boot_flow.h"

#include <stddef.h>

#include "boot/boot_mailbox.h"
#include "boot/runtime_image.h"
#include "firmware/boot_request.h"
#include "logging.h"
#include "update/update_service.h"

boot_flow_result_t BootFlow_Run(uint32_t *vector_address)
{
    BootRequestMessage_t request;
    update_result_t result;
    firmware_status_t status;

    if (vector_address == NULL)
        return BOOT_FLOW_FATAL;

    LOG_DEBUG("bootloader", "checking interrupted update");
    result = UpdateService_RecoverInterrupted();
    if (result.outcome == UPDATE_OUTCOME_RECOVERED)
    {
        LOG_INFO("bootloader", "update recovery completed");
        return BOOT_FLOW_RESET;
    }
    if (result.outcome == UPDATE_OUTCOME_RUNTIME_UNSAFE)
    {
        LOG_ERROR("bootloader", "recovery failed: stage=%u status=%u",
                  (unsigned) result.failure, (unsigned) result.status);
        return BOOT_FLOW_FATAL;
    }

    status = UpdateBootMailbox_Take(&request);
    if (status == FIRMWARE_STATUS_OK)
    {
        LOG_DEBUG("bootloader", "installing requested update");
        result = UpdateService_Install(&request);
        if (result.outcome == UPDATE_OUTCOME_INSTALLED ||
            result.outcome == UPDATE_OUTCOME_RECOVERED)
        {
            LOG_INFO("bootloader", "update transaction completed: outcome=%u",
                     (unsigned) result.outcome);
            return BOOT_FLOW_RESET;
        }
        if (result.outcome == UPDATE_OUTCOME_RUNTIME_UNSAFE)
        {
            LOG_ERROR("bootloader", "update left runtime unsafe: stage=%u status=%u",
                      (unsigned) result.failure, (unsigned) result.status);
            return BOOT_FLOW_FATAL;
        }
        if (FirmwareStatus_IsError(result.status))
            LOG_WARN("bootloader", "update rejected: stage=%u status=%u",
                     (unsigned) result.failure, (unsigned) result.status);
    }
    else if (status != FIRMWARE_STATUS_NOT_FOUND)
    {
        return BOOT_FLOW_FATAL;
    }

    status = RuntimeImage_Prepare(vector_address);
    return FirmwareStatus_IsOk(status) ? BOOT_FLOW_LAUNCH : BOOT_FLOW_FATAL;
}
