#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

#include "boot_error.h"

BootError Boot_Jump_ToAddress(uint32_t vector_table_address);

#endif
