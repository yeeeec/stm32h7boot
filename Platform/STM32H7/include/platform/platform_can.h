#ifndef PLATFORM_CAN_H
#define PLATFORM_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware/status.h"
#include "ports/can_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Lifecycle calls are Platform-owned and are not exposed via the Shared
     * can_transport_port_t consumed by Communication. */
    firmware_status_t Platform_CanStart(void);
    firmware_status_t Platform_CanStop(void);
    firmware_status_t Platform_CanRecover(void);
    firmware_status_t Platform_CanSend(const can_transport_frame_t *frame);
    bool Platform_CanRead(can_transport_frame_t *frame);
    can_transport_state_t Platform_CanGetState(void);
    void Platform_CanGetStatistics(can_transport_statistics_t *statistics);

#ifdef __cplusplus
}
#endif

#endif
