#ifndef APPLICATION_H
#define APPLICATION_H

#include <stdint.h>

#include "firmware/status.h"
#include "ports/log_output.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t Application_Init(void);

#ifdef __cplusplus
}
#endif

#endif
