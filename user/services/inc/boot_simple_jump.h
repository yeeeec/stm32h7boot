#ifndef BOOT_SIMPLE_JUMP_H
#define BOOT_SIMPLE_JUMP_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"

BootError Boot_SimpleJump_ValidateSlot(uint8_t slot, uint32_t expected_size,
                                       uint32_t expected_crc);
bool Boot_SimpleJump_IsSlotValid(uint8_t slot);
bool Boot_SimpleJump_IsSlotValidWithMetadata(uint8_t slot, uint32_t expected_size,
                                             uint32_t expected_crc);
BootError Boot_SimpleJump_ToSlot(uint8_t slot);
bool Boot_SimpleJump_IsAppValid(void);
BootError Boot_SimpleJump_ToApp(void);

#endif
