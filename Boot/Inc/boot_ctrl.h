#ifndef BOOT_CTRL_H
#define BOOT_CTRL_H

#include "boot_error.h"
#include "boot_types.h"

void Boot_Ctrl_InitDefault(BootControlBlock *ctrl);
BootError Boot_Ctrl_Load(BootControlBlock *ctrl);
BootError Boot_Ctrl_Save(BootControlBlock *ctrl);
BootSlot Boot_Ctrl_GetInactiveSlot(const BootControlBlock *ctrl);
BootImageRecord *Boot_Ctrl_GetSlotRecord(BootControlBlock *ctrl, BootSlot slot);
const BootImageRecord *Boot_Ctrl_GetSlotRecordConst(const BootControlBlock *ctrl, BootSlot slot);
BootError Boot_Ctrl_PreparePendingSlot(BootControlBlock *ctrl, BootSlot slot, const BootImageRecord *record);
BootError Boot_Ctrl_CommitPendingSlot(BootControlBlock *ctrl);
BootError Boot_Ctrl_RequestRollback(BootControlBlock *ctrl, BootError reason);
void Boot_Ctrl_IncrementPendingAttempts(BootControlBlock *ctrl);

#endif
