#include "application/application.h"

#include <stdint.h>

#include "bootloader_config.h"
#include "boot_flow.h"
#include "logging.h"
#include "platform/platform.h"
#include "platform/platform_cpu.h"
#include "platform/platform_system.h"
#include "update/update_service.h"

static uint8_t s_initialized = 0;

firmware_status_t Application_Init(void)
{
    firmware_status_t status;
    if (s_initialized != 0U)
        return FIRMWARE_STATUS_OK;
    status = Platform_Init();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "platform init failed: status=%u", (unsigned) status);
        return status;
    }
    status = PlatformSystem_WatchdogInit();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "watchdog init failed: status=%u", (unsigned) status);
        return status;
    }
    status = UpdateService_Init();
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("boot", "update service init failed: status=%u", (unsigned) status);
        return status;
    }
    s_initialized = 1U;
    LOG_INFO("boot", "bootloader init complete");
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Run(void)
{
    boot_flow_result_t result;
    uint32_t vector_address = 0U;

    if (s_initialized == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;

    result = BootFlow_Run(&vector_address);
    if (result == BOOT_FLOW_RESET)
    {
        LOG_INFO("boot", "update flow requests reset");
        PlatformSystem_Reset();
        return FIRMWARE_STATUS_OK;
    }
    if (result == BOOT_FLOW_LAUNCH)
    {
        LOG_INFO("boot", "jumping to application: vector=0x%08lx", (unsigned long) vector_address);
        PlatformCpu_Jump(vector_address);
    }

    LOG_ERROR("boot", "boot flow stopped without application jump: result=%u", (unsigned) result);
    return FIRMWARE_STATUS_INVALID_STATE;
}
