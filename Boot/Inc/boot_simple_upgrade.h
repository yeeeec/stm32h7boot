#ifndef BOOT_SIMPLE_UPGRADE_H
#define BOOT_SIMPLE_UPGRADE_H

#include "boot_error.h"
#include "boot_simple_manifest.h"

BootError Boot_SimpleUpgrade_Run(const BootManifestOperation *operation);

#endif
