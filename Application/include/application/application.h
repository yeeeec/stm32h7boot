#ifndef APPLICATION_H
#define APPLICATION_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t Application_Init(void);
    firmware_status_t Application_Run(void);

#ifdef __cplusplus
}
#endif

#endif
