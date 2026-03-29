#ifndef BOOT_SIMPLE_UPGRADE_H
#define BOOT_SIMPLE_UPGRADE_H

#include <stdint.h>

#include "boot_error.h"
#include "boot_simple_manifest.h"

BootError Boot_SimpleUpgrade_Run(const BootAppImageInfo *image, uint8_t target_slot);
BootError Boot_SimpleUpgrade_ComputeSlotCrc(uint8_t slot, uint32_t image_size, uint32_t *crc32);
BootError Boot_SimpleUpgrade_VerifySlotData(uint8_t slot, uint32_t expected_size,
                                            uint32_t expected_crc);
BootError Boot_SimpleUpgrade_CopySlot(uint8_t source_slot, uint8_t target_slot,
                                      uint32_t image_size, uint32_t expected_crc);

#endif
