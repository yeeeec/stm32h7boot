#ifndef BOOT_UPGRADE_H
#define BOOT_UPGRADE_H

#include "boot_ctrl.h"
#include "boot_error.h"
#include "boot_types.h"

BootError Boot_Upgrade_BuildPlan(const BootControlBlock *ctrl, const BootManifest *manifest, BootUpgradePlan *plan);
BootError Boot_Upgrade_Execute(BootControlBlock *ctrl, const BootManifest *manifest, const BootUpgradePlan *plan);

#endif
