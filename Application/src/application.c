/**
 * @file application.c
 * @brief Reserved Bootloader orchestration lifecycle implementation.
 */
#include "application/application.h"

static int application_initialized;

firmware_status_t Application_Init(void)
{
    if (application_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    application_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Process(void)
{
    if (application_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    return FIRMWARE_STATUS_OK;
}
