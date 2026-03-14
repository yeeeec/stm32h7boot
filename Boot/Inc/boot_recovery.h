#ifndef BOOT_RECOVERY_H
#define BOOT_RECOVERY_H

#include "boot_error.h"
#include "boot_types.h"

void Boot_Recovery_Enter(BootContext *context, BootError reason);
BootState Boot_Recovery_Process(BootContext *context);

#endif
