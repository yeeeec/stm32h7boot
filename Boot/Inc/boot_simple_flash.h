#ifndef BOOT_SIMPLE_FLASH_H
#define BOOT_SIMPLE_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"

typedef struct
{
  uint32_t base;
  uint32_t size;
} BootSimpleFlashRegion;

const BootSimpleFlashRegion *Boot_SimpleFlash_GetAppRegion(void);
bool Boot_SimpleFlash_IsAppRange(uint32_t address, uint32_t size);
BootError Boot_SimpleFlash_Read(uint32_t address, void *buffer, uint32_t size);
BootError Boot_SimpleFlash_Write(uint32_t address, const void *data, uint32_t size);
BootError Boot_SimpleFlash_EraseApp(void);

#endif
