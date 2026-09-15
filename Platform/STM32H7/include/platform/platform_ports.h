#ifndef PLATFORM_PORTS_H
#define PLATFORM_PORTS_H

#include <stdint.h>

#include "firmware/status.h"
#include "ports/backlight.h"
#include "ports/can_transport.h"
#include "ports/clock.h"
#include "ports/log_output.h"
#include "ports/parameter_storage.h"
#include "ports/runtime.h"
#include "ports/touch_input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Capability-oriented resolvers. Concrete board/device names stay below
     * this boundary in the Platform implementation and BSP. */
    firmware_status_t Platform_GetCanTransportPort(can_transport_port_t *port);
    firmware_status_t Platform_GetClockPort(clock_port_t *port);
    firmware_status_t Platform_GetRuntimePort(runtime_port_t *port);
    firmware_status_t Platform_GetLogPort(log_output_kind_t kind, log_output_port_t *port);
    firmware_status_t Platform_GetTouchPort(touch_input_port_t *port);
    firmware_status_t Platform_GetParameterStoragePort(parameter_storage_port_t *port);
    firmware_status_t Platform_GetBackLightPort(backlight_port_t *port);

#ifdef __cplusplus
}
#endif

#endif
