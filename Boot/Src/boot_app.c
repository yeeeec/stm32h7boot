#include "boot_app.h"

#include <string.h>

#include "stm32h7xx_hal.h"

#include "boot_ctrl.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_log.h"
#include "boot_manifest.h"
#include "boot_recovery.h"
#include "boot_upgrade.h"
#include "boot_usb.h"

static BootContext g_boot_context;

static void Boot_App_EnterState(BootState state)
{
  g_boot_context.state = state;
  g_boot_context.state_entry_tick = HAL_GetTick();
}

static BootSlot Boot_App_OtherSlot(BootSlot slot)
{
  if (slot == BOOT_SLOT_APP1)
  {
    return BOOT_SLOT_APP2;
  }
  if (slot == BOOT_SLOT_APP2)
  {
    return BOOT_SLOT_APP1;
  }
  return BOOT_SLOT_NONE;
}

static void Boot_App_EnterRecovery(BootError reason)
{
  Boot_Recovery_Enter(&g_boot_context, reason);
  Boot_App_EnterState(BOOT_STATE_RECOVERY);
}

static BootError Boot_App_TryBootSlot(BootSlot slot)
{
  BootImageRecord record;
  BootError error;

  error = Boot_Image_ValidateSlot(slot, &record);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  g_boot_context.slot_to_boot = slot;
  if (g_boot_context.ctrl.active_slot == (uint8_t)slot)
  {
    return BOOT_ERR_NONE;
  }

  g_boot_context.ctrl.active_slot = (uint8_t)slot;
  return Boot_Ctrl_Save(&g_boot_context.ctrl);
}

void Boot_App_Init(void)
{
  memset(&g_boot_context, 0, sizeof(g_boot_context));
  g_boot_context.mode = BOOT_MODE_NORMAL;
  g_boot_context.slot_to_boot = BOOT_SLOT_NONE;
  Boot_App_EnterState(BOOT_STATE_INIT);
}

const BootContext *Boot_App_GetContext(void)
{
  return &g_boot_context;
}

void Boot_App_Process(void)
{
  BootError error;
  BootUsbScanResult scan_result;
  BootSlot other_slot;

  switch (g_boot_context.state)
  {
    case BOOT_STATE_INIT:
      Boot_Flash_Init();
      Boot_Log_Init();
      Boot_Usb_Init();
      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "Boot framework init");
      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: INIT -> LOAD_CTRL");
      Boot_App_EnterState(BOOT_STATE_LOAD_CTRL);
      break;

    case BOOT_STATE_LOAD_CTRL:
      error = Boot_Ctrl_Load(&g_boot_context.ctrl);
      if (error != BOOT_ERR_NONE)
      {
        g_boot_context.last_error = (uint32_t)error;
        Boot_Log_Write(BOOT_LOG_WARN, error, "Control block load fallback");
        Boot_Ctrl_InitDefault(&g_boot_context.ctrl);
        (void)Boot_Ctrl_Save(&g_boot_context.ctrl);
      }
      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: LOAD_CTRL -> USB_WAIT");
      Boot_App_EnterState(BOOT_STATE_USB_WAIT);
      break;

    case BOOT_STATE_USB_WAIT:
      scan_result = Boot_Usb_PollForUpgradeMedia(BOOT_USB_WAIT_WINDOW_MS, &error);
      if (scan_result == BOOT_USB_SCAN_WAITING)
      {
        break;
      }

      if (scan_result == BOOT_USB_SCAN_UPGRADE_READY)
      {
        g_boot_context.mode = BOOT_MODE_UPGRADE;
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "Upgrade media detected");
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: USB_WAIT -> MANIFEST_PARSE");
        Boot_App_EnterState(BOOT_STATE_MANIFEST_PARSE);
        break;
      }

      if (scan_result == BOOT_USB_SCAN_ERROR)
      {
        g_boot_context.last_error = (uint32_t)error;
        Boot_Log_Write(BOOT_LOG_WARN, error, "USB scan failed");
      }

      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: USB_WAIT -> ARBITRATE");
      Boot_App_EnterState(BOOT_STATE_ARBITRATE);
      break;

    case BOOT_STATE_MANIFEST_PARSE:
      error = Boot_Manifest_Load(&g_boot_context.manifest);
      if (error != BOOT_ERR_NONE)
      {
        g_boot_context.last_error = (uint32_t)error;
        Boot_Log_Write(BOOT_LOG_WARN, error, "Manifest not ready");
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: MANIFEST_PARSE -> ARBITRATE");
        Boot_App_EnterState(BOOT_STATE_ARBITRATE);
        break;
      }

      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "Manifest parsed");
      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: MANIFEST_PARSE -> UPGRADE_EXECUTE");
      Boot_App_EnterState(BOOT_STATE_UPGRADE_EXECUTE);
      break;

    case BOOT_STATE_UPGRADE_EXECUTE:
      error = Boot_Upgrade_BuildPlan(&g_boot_context.ctrl, &g_boot_context.manifest, &g_boot_context.plan);
      if (error == BOOT_ERR_NONE)
      {
        error = Boot_Upgrade_Execute(&g_boot_context.ctrl, &g_boot_context.manifest, &g_boot_context.plan);
      }

      if (error != BOOT_ERR_NONE)
      {
        g_boot_context.last_error = (uint32_t)error;
        Boot_Log_Write(BOOT_LOG_ERROR, error, "Upgrade execute failed");
      }
      else
      {
        Boot_Log_Write(BOOT_LOG_RESULT, BOOT_ERR_NONE, "Upgrade execute finished");
      }

      Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: UPGRADE_EXECUTE -> ARBITRATE");
      Boot_App_EnterState(BOOT_STATE_ARBITRATE);
      break;

    case BOOT_STATE_ARBITRATE:
      if (g_boot_context.ctrl.pending_slot != BOOT_SLOT_NONE)
      {
        if (g_boot_context.ctrl.boot_attempts < g_boot_context.ctrl.max_boot_attempts)
        {
          error = Boot_App_TryBootSlot((BootSlot)g_boot_context.ctrl.pending_slot);
          if (error == BOOT_ERR_NONE)
          {
            Boot_Ctrl_IncrementPendingAttempts(&g_boot_context.ctrl);
            (void)Boot_Ctrl_Save(&g_boot_context.ctrl);
            Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: ARBITRATE -> JUMP (pending slot)");
            Boot_App_EnterState(BOOT_STATE_JUMP);
            break;
          }
        }

        error = Boot_Ctrl_RequestRollback(&g_boot_context.ctrl, BOOT_ERR_PENDING_SLOT_EXHAUSTED);
        if (error != BOOT_ERR_NONE)
        {
          Boot_App_EnterRecovery(error);
          break;
        }
        (void)Boot_Ctrl_Save(&g_boot_context.ctrl);
      }

      error = Boot_App_TryBootSlot((BootSlot)g_boot_context.ctrl.active_slot);
      if (error == BOOT_ERR_NONE)
      {
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: ARBITRATE -> JUMP (active slot)");
        Boot_App_EnterState(BOOT_STATE_JUMP);
        break;
      }

      other_slot = Boot_App_OtherSlot((BootSlot)g_boot_context.ctrl.active_slot);
      error = Boot_App_TryBootSlot(other_slot);
      if (error == BOOT_ERR_NONE)
      {
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: ARBITRATE -> JUMP (fallback slot)");
        Boot_App_EnterState(BOOT_STATE_JUMP);
        break;
      }

      Boot_App_EnterRecovery(BOOT_ERR_NO_BOOTABLE_IMAGE);
      break;

    case BOOT_STATE_JUMP:
      Boot_Log_Write(BOOT_LOG_RESULT, BOOT_ERR_NONE, "Jump to selected slot");
      error = Boot_Jump_ToSlot(g_boot_context.slot_to_boot);
      g_boot_context.last_error = (uint32_t)error;
      Boot_Log_Write(BOOT_LOG_ERROR, error, "Jump failed");
      Boot_App_EnterRecovery(error);
      break;

    case BOOT_STATE_RECOVERY:
      if (Boot_Recovery_Process(&g_boot_context) == BOOT_STATE_MANIFEST_PARSE)
      {
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "Recovery media detected");
        Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, "State transition: RECOVERY -> MANIFEST_PARSE");
        Boot_App_EnterState(BOOT_STATE_MANIFEST_PARSE);
      }
      break;

    default:
      Boot_App_EnterRecovery(BOOT_ERR_CTRL_CORRUPTED);
      break;
  }
}
