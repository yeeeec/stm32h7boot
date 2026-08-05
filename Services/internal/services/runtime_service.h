#ifndef RUNTIME_SERVICE_H
#define RUNTIME_SERVICE_H

#include <stdint.h>

#include "firmware/log_sink.h"
#include "firmware/status.h"
#include "firmware/system_clock.h"
#include "firmware/watchdog.h"
#include "services/runtime_service_api.h"

typedef struct
{
    const system_clock_t *clock;
    const watchdog_t *watchdog;
    const log_sink_t *log;
} runtime_service_dependencies_t;

typedef struct runtime_service
{
    const system_clock_t *clock;
    const watchdog_t *watchdog;
    const log_sink_t *log;
    uint32_t last_watchdog_refresh_ms;
    int initialized;
} runtime_service_t;

firmware_status_t RuntimeService_Init(
    runtime_service_t *service,
    const runtime_service_dependencies_t *dependencies);

#endif
