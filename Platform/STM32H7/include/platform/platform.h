#ifndef PLATFORM_H
#define PLATFORM_H

#include "firmware/status.h"

firmware_status_t Platform_Init(void);
void Platform_Process(void);
int Platform_IsInitialized(void);

#endif
