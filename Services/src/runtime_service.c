#include "services/runtime_service.h"

#include <stddef.h>

#define WATCHDOG_REFRESH_INTERVAL_MS 100U

static const uint8_t startup_message[] = "firmware runtime initialized\r\n";

firmware_status_t RuntimeService_Init(
    runtime_service_t *service,
    const runtime_service_dependencies_t *dependencies)
{
    firmware_status_t status;

    if ((service == NULL) || (dependencies == NULL) ||
        (dependencies->clock == NULL) ||
        (dependencies->clock->now_ms == NULL) ||
        (dependencies->watchdog == NULL) ||
        (dependencies->watchdog->refresh == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->clock = dependencies->clock;
    service->watchdog = dependencies->watchdog;
    service->log = dependencies->log;
    service->last_watchdog_refresh_ms = service->clock->now_ms(
        service->clock->context);

    status = service->watchdog->refresh(service->watchdog->context);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    if ((service->log != NULL) && (service->log->write != NULL))
    {
        status = service->log->write(
            service->log->context,
            startup_message,
            sizeof(startup_message) - 1U);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
    }

    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RuntimeService_Process(runtime_service_t *service)
{
    uint32_t now_ms;

    if ((service == NULL) || (service->initialized == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    now_ms = service->clock->now_ms(service->clock->context);
    if ((uint32_t)(now_ms - service->last_watchdog_refresh_ms) >=
        WATCHDOG_REFRESH_INTERVAL_MS)
    {
        firmware_status_t status = service->watchdog->refresh(
            service->watchdog->context);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        service->last_watchdog_refresh_ms = now_ms;
    }

    return FIRMWARE_STATUS_OK;
}
