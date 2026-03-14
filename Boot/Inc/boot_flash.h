#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"
#include "boot_types.h"

void Boot_Flash_Init(void);
const BootPartition *Boot_Flash_GetPartition(BootSlot slot);
bool Boot_Flash_IsAddressInRange(uint32_t address, uint32_t size, uint32_t base, uint32_t range_size);
BootError Boot_Flash_LoadControlBlock(BootControlBlock *ctrl);
BootError Boot_Flash_SaveControlBlock(const BootControlBlock *ctrl);
BootError Boot_Flash_Read(uint32_t address, void *buffer, uint32_t size);
BootError Boot_Flash_Write(uint32_t address, const void *data, uint32_t size);
BootError Boot_Flash_Erase(uint32_t address, uint32_t size);

#endif
