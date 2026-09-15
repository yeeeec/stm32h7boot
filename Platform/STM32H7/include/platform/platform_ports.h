#ifndef PLATFORM_PORTS_H
#define PLATFORM_PORTS_H

#include "firmware/status.h"
#include "ports/clock.h"
#include "ports/log_output.h"
#include "ports/parameter_storage.h"
#include "ports/runtime.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Bare-metal capabilities currently required by Boot. Other App-only ports
 * (CAN, touch, display, and USB) remain outside this target until their
 * corresponding CubeMX resources are intentionally enabled. */
firmware_status_t Platform_GetClockPort(clock_port_t *port);
firmware_status_t Platform_GetRuntimePort(runtime_port_t *port);
firmware_status_t Platform_GetLogPort(log_output_kind_t kind, log_output_port_t *port);
firmware_status_t Platform_GetParameterStoragePort(parameter_storage_port_t *port);

#ifdef __cplusplus
}
#endif

#endif
