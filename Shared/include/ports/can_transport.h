#ifndef FIRMWARE_CAN_TRANSPORT_H
#define FIRMWARE_CAN_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CAN_TRANSPORT_MAX_DATA_BYTES 8U

    typedef struct
    {
        uint32_t id;
        uint8_t dlc;
        uint8_t reserved[3];
        uint8_t data[CAN_TRANSPORT_MAX_DATA_BYTES];
    } can_transport_frame_t;

    typedef enum
    {
        CAN_TRANSPORT_STATE_UNINITIALIZED = 0,
        CAN_TRANSPORT_STATE_STOPPED,
        CAN_TRANSPORT_STATE_RUNNING,
        CAN_TRANSPORT_STATE_BUS_OFF,
        CAN_TRANSPORT_STATE_ERROR
    } can_transport_state_t;

    typedef struct
    {
        uint32_t rx_frames;
        uint32_t tx_frames;
        uint32_t rx_overflow;
        uint32_t bus_off_count;
        uint32_t hw_error_count;
        uint32_t tx_busy_count;
        uint32_t rx_read_errors;
    } can_transport_statistics_t;

    typedef struct
    {
        void *context;
        /* The controller lifecycle is owned by the composition root through
         * Platform_Init. Communication only recovers a failed controller. */
        firmware_status_t (*recover)(void *context);
        firmware_status_t (*send)(void *context, const can_transport_frame_t *frame);
        bool (*read)(void *context, can_transport_frame_t *frame);
        can_transport_state_t (*get_state)(void *context);
        void (*get_statistics)(void *context, can_transport_statistics_t *statistics);
    } can_transport_port_t;

#ifdef __cplusplus
}
#endif

#endif
