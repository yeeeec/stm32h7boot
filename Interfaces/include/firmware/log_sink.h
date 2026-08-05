#ifndef FIRMWARE_LOG_SINK_H
#define FIRMWARE_LOG_SINK_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

typedef firmware_status_t (*log_sink_write_fn)(
    void *context,
    const uint8_t *data,
    size_t size);

typedef struct
{
    void *context;
    log_sink_write_fn write;
} log_sink_t;

#endif
