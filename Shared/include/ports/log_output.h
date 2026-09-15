#ifndef FIRMWARE_LOG_OUTPUT_PORT_H
#define FIRMWARE_LOG_OUTPUT_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    LOG_OUTPUT_UART = 0,
    LOG_OUTPUT_RTT  = 1
} log_output_kind_t;

typedef firmware_status_t (*log_output_write_fn)(void *context, const uint8_t *data,
                                                 size_t size);
typedef uint32_t (*log_output_now_ms_fn)(void *context);

typedef struct
{
    void *context;
    log_output_write_fn write;
    log_output_now_ms_fn now_ms;
} log_output_port_t;

typedef firmware_status_t (*log_output_resolver_fn)(void *context, log_output_kind_t kind,
                                                    log_output_port_t *port);

#ifdef __cplusplus
}
#endif

#endif
