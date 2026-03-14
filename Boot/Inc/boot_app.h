#ifndef BOOT_APP_H
#define BOOT_APP_H

#include "boot_types.h"

void Boot_App_Init(void);
void Boot_App_Process(void);
const BootContext *Boot_App_GetContext(void);

#endif
