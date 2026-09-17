#ifndef PLATFORM_LOG_H
#define PLATFORM_LOG_H

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


    firmware_status_t Platform_LogInit(void);

    firmware_status_t Platform_SetLogPort(log_output_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif
