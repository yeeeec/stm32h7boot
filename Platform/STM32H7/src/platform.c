/**
 * @file platform.c
 * @brief STM32H7 platform lifecycle implementation.
 */
#include "platform/platform.h"

#include "platform/platform_reset_reason.h"

static int platform_initialized;

firmware_status_t Platform_Init(void)
{
    if (platform_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    Platform_ResetReasonCapture();
    platform_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

void Platform_Process(void)
{
}

int Platform_IsInitialized(void)
{
    return platform_initialized;
}
