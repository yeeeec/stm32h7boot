#ifndef BOOT_EXTFLASH_H
#define BOOT_EXTFLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"

bool Boot_ExtFlash_IsRangeValid(uint32_t address, uint32_t size);
BootError Boot_ExtFlash_Init(void);
BootError Boot_ExtFlash_Read(uint32_t address, void *buffer, uint32_t size);
BootError Boot_ExtFlash_Write(uint32_t address, const void *data, uint32_t size);
BootError Boot_ExtFlash_Erase(uint32_t address, uint32_t size);

#endif
