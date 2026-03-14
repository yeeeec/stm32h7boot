#include "boot_recovery.h"

#include <stdio.h>

#include "stm32h7xx_hal.h"
#include "boot_log.h"
#include "boot_usb.h"

static BootError g_boot_recovery_last_reason = BOOT_ERR_NONE;
static uint8_t g_boot_recovery_retry_level;
static uint32_t g_boot_recovery_retry_deadline_ms;

static uint32_t Boot_Recovery_GetRetryDelayMs(uint8_t retry_level)
{
  uint32_t delay_ms = BOOT_RECOVERY_POLL_MS;

  while ((retry_level > 0U) && (delay_ms < 8000UL))
  {
    delay_ms <<= 1U;
    retry_level--;
  }

  if (delay_ms > 8000UL)
  {
    delay_ms = 8000UL;
  }

  return delay_ms;
}

void Boot_Recovery_Enter(BootContext *context, BootError reason)
{
  uint32_t retry_delay_ms;
  char log_message[BOOT_LOG_MESSAGE_MAX_LENGTH];

  if (context == NULL)
  {
    return;
  }

  if (reason == g_boot_recovery_last_reason)
  {
    if (g_boot_recovery_retry_level < 3U)
    {
      g_boot_recovery_retry_level++;
    }
  }
  else
  {
    g_boot_recovery_last_reason = reason;
    g_boot_recovery_retry_level = 0U;
  }

  retry_delay_ms = Boot_Recovery_GetRetryDelayMs(g_boot_recovery_retry_level);
  g_boot_recovery_retry_deadline_ms = HAL_GetTick() + retry_delay_ms;

  context->mode = BOOT_MODE_RECOVERY;
  context->last_error = (uint32_t)reason;
  Boot_Log_Write(BOOT_LOG_ERROR, reason, "Enter recovery mode");
  (void)snprintf(log_message,
                 sizeof(log_message),
                 "Recovery retry delay: %lu ms",
                 (unsigned long)retry_delay_ms);
  Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, log_message);
}

BootState Boot_Recovery_Process(BootContext *context)
{
  BootError scan_error;
  BootUsbScanResult scan_result;
  uint32_t now_ms;

  if (context == NULL)
  {
    return BOOT_STATE_RECOVERY;
  }

  now_ms = HAL_GetTick();
  if ((int32_t)(now_ms - g_boot_recovery_retry_deadline_ms) < 0)
  {
    return BOOT_STATE_RECOVERY;
  }

  scan_result = Boot_Usb_PollForUpgradeMedia(BOOT_RECOVERY_POLL_MS, &scan_error);
  if (scan_result == BOOT_USB_SCAN_UPGRADE_READY)
  {
    context->mode = BOOT_MODE_FORCE_UPGRADE;
    g_boot_recovery_retry_level = 0U;
    return BOOT_STATE_MANIFEST_PARSE;
  }

  if (scan_result == BOOT_USB_SCAN_ERROR)
  {
    context->last_error = (uint32_t)scan_error;
  }

  return BOOT_STATE_RECOVERY;
}
