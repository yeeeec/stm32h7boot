#ifndef PLATFORM_H
#define PLATFORM_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize only the always-needed Platform bindings. */
firmware_status_t Platform_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
