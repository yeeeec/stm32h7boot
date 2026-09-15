#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware/status.h"
#include "ports/can_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t BSP_CAN_Init(void);
    firmware_status_t BSP_CAN_Start(void);
    firmware_status_t BSP_CAN_Stop(void);
    firmware_status_t BSP_CAN_Recover(void);
    firmware_status_t BSP_CAN_Send(const can_transport_frame_t *frame);
    bool BSP_CAN_Read(can_transport_frame_t *frame);
    can_transport_state_t BSP_CAN_GetState(void);
    void BSP_CAN_GetStatistics(can_transport_statistics_t *statistics);

#ifdef __cplusplus
}
#endif

#endif
