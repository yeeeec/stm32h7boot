#ifndef BOOT_EXTFLASH_H
#define BOOT_EXTFLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"

bool Boot_ExtFlash_IsRangeAllowed(uint32_t offset, uint32_t size);
BootError Boot_ExtFlash_Erase(uint32_t offset, uint32_t size);
BootError Boot_ExtFlash_Write(uint32_t offset, const void *data, uint32_t size);
BootError Boot_ExtFlash_DumpToUsb(uint32_t offset, uint32_t size, const char *relative_path);

#endif
