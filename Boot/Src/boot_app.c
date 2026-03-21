#include "boot_app.h"

#include <string.h>

#include "boot_log.h"
#include "boot_platform.h"
#include "boot_simple_jump.h"
#include "boot_simple_manifest.h"
#include "boot_simple_upgrade.h"
#include "boot_usb.h"

typedef enum
{
  BOOT_APP_STATE_INIT = 0,
  BOOT_APP_STATE_USB_SCAN,
  BOOT_APP_STATE_MANIFEST_LOAD,
  BOOT_APP_STATE_UPGRADE,
  BOOT_APP_STATE_JUMP,
  BOOT_APP_STATE_FATAL
} BootAppState;

typedef struct
{
  BootAppState state;
  BootError last_error;
  BootManifest manifest;
  const BootManifestOperation *selected_operation;
} BootAppContext;

static BootAppContext g_boot_app;

static void Boot_App_FeedWatchdogForever(void)
{
  while (1)
  {
    Boot_Platform_FeedWatchdog();
  }
}

static void Boot_App_EnterFatal(BootError error)
{
  g_boot_app.state = BOOT_APP_STATE_FATAL;
  g_boot_app.last_error = error;
  LOG_ERROR(BOOT_LOG_TAG, "Fatal boot error: %s", Boot_ErrorToString(error));
  Boot_App_FeedWatchdogForever();
}

void Boot_App_Init(void)
{
  memset(&g_boot_app, 0, sizeof(g_boot_app));
  g_boot_app.state = BOOT_APP_STATE_INIT;
}

void Boot_App_Process(void)
{
  BootError error = BOOT_ERR_NONE;

  switch (g_boot_app.state)
  {
    case BOOT_APP_STATE_INIT:
      Boot_Log_Init();
      Boot_Usb_Init();
      LOG_INFO(BOOT_LOG_TAG, "Minimal boot flow init");
      g_boot_app.state = BOOT_APP_STATE_USB_SCAN;
      break;

    case BOOT_APP_STATE_USB_SCAN:
    {
      BootUsbScanResult scan_result = Boot_Usb_PollForUpgradeMedia(BOOT_USB_WAIT_WINDOW_MS, &error);

      if (scan_result == BOOT_USB_SCAN_WAITING)
      {
        return;
      }

      if (scan_result == BOOT_USB_SCAN_UPGRADE_READY)
      {
        LOG_INFO(BOOT_LOG_TAG, "Special U-disk detected");
        g_boot_app.state = BOOT_APP_STATE_MANIFEST_LOAD;
        return;
      }

      if (error != BOOT_ERR_NONE)
      {
        LOG_INFO(BOOT_LOG_TAG, "Skip upgrade media: %s", Boot_ErrorToString(error));
      }
      else
      {
        LOG_INFO(BOOT_LOG_TAG, "No upgrade media, jump to app");
      }

      g_boot_app.state = BOOT_APP_STATE_JUMP;
      return;
    }

    case BOOT_APP_STATE_MANIFEST_LOAD:
      error = Boot_SimpleManifest_Load(&g_boot_app.manifest);
      if (error != BOOT_ERR_NONE)
      {
        LOG_WARN(BOOT_LOG_TAG, "Manifest ignored: %s", Boot_ErrorToString(error));
        g_boot_app.state = BOOT_APP_STATE_JUMP;
        return;
      }

      g_boot_app.selected_operation = Boot_SimpleManifest_FindFirstSupported(&g_boot_app.manifest);
      if (g_boot_app.selected_operation == NULL)
      {
        LOG_INFO(BOOT_LOG_TAG, "No supported bin found under %s", BOOT_USB_CRC_DIR);
        g_boot_app.state = BOOT_APP_STATE_JUMP;
        return;
      }

      LOG_INFO(BOOT_LOG_TAG,
               "Selected upgrade item: %s size=%lu crc=0x%08lX",
               g_boot_app.selected_operation->file,
               (unsigned long)g_boot_app.selected_operation->size,
               (unsigned long)g_boot_app.selected_operation->crc32);
      g_boot_app.state = BOOT_APP_STATE_UPGRADE;
      return;

    case BOOT_APP_STATE_UPGRADE:
      error = Boot_SimpleUpgrade_Run(g_boot_app.selected_operation);
      if (error != BOOT_ERR_NONE)
      {
        Boot_App_EnterFatal(error);
        return;
      }

      LOG_INFO(BOOT_LOG_TAG, "Upgrade finished, jump to app");
      g_boot_app.state = BOOT_APP_STATE_JUMP;
      return;

    case BOOT_APP_STATE_JUMP:
      if (Boot_Usb_IsMounted())
      {
        Boot_Platform_UsbUnmount();
      }

      error = Boot_SimpleJump_ToApp();
      Boot_App_EnterFatal(error);
      return;

    case BOOT_APP_STATE_FATAL:
      Boot_App_FeedWatchdogForever();
      return;

    default:
      Boot_App_EnterFatal(BOOT_ERR_INVALID_ARGUMENT);
      return;
  }
}
