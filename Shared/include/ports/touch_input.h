#ifndef FIRMWARE_TOUCH_INPUT_PORT_H
#define FIRMWARE_TOUCH_INPUT_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint8_t has_update;
    uint8_t detected;
    uint16_t x;
    uint16_t y;
} touch_input_state_t;

typedef struct
{
    void *context;
    firmware_status_t (*read)(void *context, touch_input_state_t *state);
    bool (*is_ready)(void *context);
} touch_input_port_t;

#ifdef __cplusplus
}
#endif

#endif
