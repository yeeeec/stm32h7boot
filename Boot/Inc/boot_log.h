#ifndef BOOT_LOG_H
#define BOOT_LOG_H

#include <stddef.h>

#include "boot_error.h"
#include "boot_types.h"

void Boot_Log_Init(void);
void Boot_Log_SetLevel(BootLogLevel level);
void Boot_Log_Write(BootLogLevel level, BootError error, const char *message);
void Boot_Log_TryFlushToUsb(void);
BootError Boot_Log_FlushToUsb(void);
BootError Boot_Log_GetLastError(void);
const BootLogEntry *Boot_Log_GetEntries(size_t *count);

#endif
