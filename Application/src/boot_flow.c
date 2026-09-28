#include "boot_flow.h"

#include <stddef.h>

#include "boot/runtime_image.h"
#include "logging.h"
#include "update/update_service.h"

firmware_status_t BootFlow_Run(uint32_t *vector_address)
{
    firmware_status_t status;

    if (vector_address == NULL)
    {
        LOG_ERROR("boot", "boot flow cannot start: vector address is null");
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = UpdateService_Process();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "update flow failed: status=%u", (unsigned)status);
        return status;
    }

    status = RuntimeImage_Prepare(vector_address);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "runtime image check failed: status=%u", (unsigned)status);
        return status;
    }
    LOG_INFO("boot", "runtime image ready: vector=0x%08lx", (unsigned long)*vector_address);
    return FIRMWARE_STATUS_OK;
}
