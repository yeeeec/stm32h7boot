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

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t slot_count;
  BootSimpleFlashRegion slot_regions[BOOT_APP_SLOT_COUNT];
  uint32_t crc;
} BootSimpleFlashLayout;

void Boot_SimpleFlash_InitDefaultLayout(BootSimpleFlashLayout *layout);
BootError Boot_SimpleFlash_LoadLayout(BootSimpleFlashLayout *layout);
BootError Boot_SimpleFlash_WriteLayout(const BootSimpleFlashLayout *layout);
BootError Boot_SimpleFlash_StoreLayout(const BootSimpleFlashLayout *layout);
BootError Boot_SimpleFlash_EnsureLayoutPersisted(void);
const BootSimpleFlashRegion *Boot_SimpleFlash_GetInfoRegion(void);
const BootSimpleFlashRegion *Boot_SimpleFlash_GetSlotRegion(uint8_t slot);
bool Boot_SimpleFlash_IsInternalFlashRange(uint32_t address, uint32_t size);
bool Boot_SimpleFlash_IsRangeInRegion(const BootSimpleFlashRegion *region, uint32_t address, uint32_t size);
BootError Boot_SimpleFlash_Read(uint32_t address, void *buffer, uint32_t size);
BootError Boot_SimpleFlash_Write(uint32_t address, const void *data, uint32_t size);
BootError Boot_SimpleFlash_EraseRegion(uint32_t address, uint32_t size);
BootError Boot_SimpleFlash_EraseInfoRegion(void);
BootError Boot_SimpleFlash_EraseSlot(uint8_t slot);

#endif
