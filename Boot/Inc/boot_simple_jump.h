#ifndef BOOT_SIMPLE_JUMP_H
#define BOOT_SIMPLE_JUMP_H

#include <stdbool.h>

#include "boot_error.h"

bool Boot_SimpleJump_IsAppValid(void);
BootError Boot_SimpleJump_ToApp(void);

#endif
