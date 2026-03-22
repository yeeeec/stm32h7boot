#ifndef BOOT_SIMPLE_UPGRADE_H
#define BOOT_SIMPLE_UPGRADE_H

#include <stdint.h>

#include "boot_error.h"
#include "boot_simple_manifest.h"

BootError Boot_SimpleUpgrade_Run(const BootAppImageInfo *image, uint8_t target_slot);

#endif
