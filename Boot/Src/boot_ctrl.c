#include "boot_ctrl.h"

#include <stddef.h>
#include <string.h>

#include "boot_crc32.h"
#include "boot_flash.h"

static uint32_t Boot_Ctrl_CalcCrc(const BootControlBlock *ctrl)
{
  return Boot_Crc32_Calc(ctrl, offsetof(BootControlBlock, crc32), 0xFFFFFFFFUL);
}

void Boot_Ctrl_InitDefault(BootControlBlock *ctrl)
{
  if (ctrl == NULL)
  {
    return;
  }

  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->magic = BOOT_CTRL_MAGIC;
  ctrl->struct_version = BOOT_CTRL_STRUCT_VERSION;
  ctrl->length = sizeof(BootControlBlock);
  ctrl->sequence = 1U;
  ctrl->active_slot = BOOT_SLOT_APP1;
  ctrl->confirmed_slot = BOOT_SLOT_APP1;
  ctrl->pending_slot = BOOT_SLOT_NONE;
  ctrl->boot_attempts = 0U;
  ctrl->max_boot_attempts = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
  ctrl->upgrade_in_progress = 0U;
  ctrl->rollback_requested = 0U;
  ctrl->last_error = BOOT_ERR_NONE;
  ctrl->crc32 = Boot_Ctrl_CalcCrc(ctrl);
}

BootError Boot_Ctrl_Load(BootControlBlock *ctrl)
{
  BootError error;

  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  error = Boot_Flash_LoadControlBlock(ctrl);
  if (error == BOOT_ERR_CTRL_NOT_FOUND)
  {
    Boot_Ctrl_InitDefault(ctrl);
    return Boot_Flash_SaveControlBlock(ctrl);
  }

  return error;
}

BootError Boot_Ctrl_Save(BootControlBlock *ctrl)
{
  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  ++ctrl->sequence;
  ctrl->length = sizeof(BootControlBlock);
  ctrl->crc32 = Boot_Ctrl_CalcCrc(ctrl);
  return Boot_Flash_SaveControlBlock(ctrl);
}

BootSlot Boot_Ctrl_GetInactiveSlot(const BootControlBlock *ctrl)
{
  if ((ctrl == NULL) || (ctrl->confirmed_slot == BOOT_SLOT_NONE))
  {
    return BOOT_SLOT_APP1;
  }

  return (ctrl->confirmed_slot == BOOT_SLOT_APP1) ? BOOT_SLOT_APP2 : BOOT_SLOT_APP1;
}

BootImageRecord *Boot_Ctrl_GetSlotRecord(BootControlBlock *ctrl, BootSlot slot)
{
  if (ctrl == NULL)
  {
    return NULL;
  }

  if (slot == BOOT_SLOT_APP1)
  {
    return &ctrl->app1;
  }
  if (slot == BOOT_SLOT_APP2)
  {
    return &ctrl->app2;
  }
  return NULL;
}

const BootImageRecord *Boot_Ctrl_GetSlotRecordConst(const BootControlBlock *ctrl, BootSlot slot)
{
  return Boot_Ctrl_GetSlotRecord((BootControlBlock *)ctrl, slot);
}

BootError Boot_Ctrl_PreparePendingSlot(BootControlBlock *ctrl, BootSlot slot, const BootImageRecord *record)
{
  BootImageRecord *slot_record;

  if ((ctrl == NULL) || (record == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  slot_record = Boot_Ctrl_GetSlotRecord(ctrl, slot);
  if (slot_record == NULL)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  *slot_record = *record;
  ctrl->pending_slot = (uint8_t)slot;
  ctrl->active_slot = (uint8_t)slot;
  ctrl->boot_attempts = 0U;
  ctrl->upgrade_in_progress = 1U;
  ctrl->rollback_requested = 0U;
  return BOOT_ERR_NONE;
}

BootError Boot_Ctrl_CommitPendingSlot(BootControlBlock *ctrl)
{
  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (ctrl->pending_slot == BOOT_SLOT_NONE)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  ctrl->confirmed_slot = ctrl->pending_slot;
  ctrl->active_slot = ctrl->pending_slot;
  ctrl->pending_slot = BOOT_SLOT_NONE;
  ctrl->boot_attempts = 0U;
  ctrl->upgrade_in_progress = 0U;
  ctrl->rollback_requested = 0U;
  ctrl->last_error = BOOT_ERR_NONE;
  return BOOT_ERR_NONE;
}

BootError Boot_Ctrl_RequestRollback(BootControlBlock *ctrl, BootError reason)
{
  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  ctrl->rollback_requested = 1U;
  ctrl->upgrade_in_progress = 0U;
  ctrl->pending_slot = BOOT_SLOT_NONE;
  ctrl->boot_attempts = 0U;
  ctrl->last_error = (uint32_t)reason;

  if (ctrl->confirmed_slot != BOOT_SLOT_NONE)
  {
    ctrl->active_slot = ctrl->confirmed_slot;
    return BOOT_ERR_NONE;
  }

  return BOOT_ERR_ROLLBACK_FAILED;
}

void Boot_Ctrl_IncrementPendingAttempts(BootControlBlock *ctrl)
{
  if ((ctrl == NULL) || (ctrl->pending_slot == BOOT_SLOT_NONE))
  {
    return;
  }

  if (ctrl->boot_attempts < 0xFFU)
  {
    ++ctrl->boot_attempts;
  }
}
