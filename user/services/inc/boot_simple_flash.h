#ifndef BOOT_SIMPLE_FLASH_H
#define BOOT_SIMPLE_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_config.h"
#include "boot_error.h"
#include "boot_info.h"

typedef struct
{
  uint32_t base;
  uint32_t size;
} BootSimpleFlashRegion;

const BootSimpleFlashRegion *Boot_SimpleFlash_GetInfoRegion(void);
const BootSimpleFlashRegion *Boot_SimpleFlash_GetSlotRegion(uint8_t slot);
bool Boot_SimpleFlash_IsInternalFlashRange(uint32_t address, uint32_t size);
bool Boot_SimpleFlash_IsExtFlashRange(uint32_t address, uint32_t size);
bool Boot_SimpleFlash_IsRangeInRegion(const BootSimpleFlashRegion *region, uint32_t address, uint32_t size);
BootError Boot_SimpleFlash_Read(uint32_t address, void *buffer, uint32_t size);
BootError Boot_SimpleFlash_Write(uint32_t address, const void *data, uint32_t size);
BootError Boot_SimpleFlash_EraseRegion(uint32_t address, uint32_t size);
BootError Boot_SimpleFlash_EraseInfoRegion(void);
BootError Boot_SimpleFlash_EraseSlot(uint8_t slot);

#endif
