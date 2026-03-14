#ifndef BOOT_MANIFEST_H
#define BOOT_MANIFEST_H

#include "boot_error.h"
#include "boot_types.h"

void Boot_Manifest_Reset(BootManifest *manifest);
BootError Boot_Manifest_Load(BootManifest *manifest);

#endif
