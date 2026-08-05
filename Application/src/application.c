#include "application/application.h"

#include <stddef.h>

#include "logging.h"
#include "services/runtime_service_api.h"

static struct runtime_service *application_runtime_service;
static int application_initialized;

firmware_status_t Application_Configure(struct runtime_service *runtime_service)
{
    if (runtime_service == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (application_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    application_runtime_service = runtime_service;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Init(void)
{
    if (application_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (application_runtime_service == NULL)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    application_initialized = 1;
    LOG_INFO("application", "application initialized");
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Process(void)
{
    if (application_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    return RuntimeService_Process(application_runtime_service);
}
