#include "boot_usb.h"

#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"
#include "boot_config.h"

extern ApplicationTypeDef Appli_state;

static uint32_t g_boot_usb_start_tick;
static uint8_t g_boot_usb_scan_started;
static uint8_t g_boot_usb_mounted;

void Boot_Usb_Init(void)
{
  g_boot_usb_start_tick = HAL_GetTick();
  g_boot_usb_scan_started = 0U;
  g_boot_usb_mounted = 0U;
}

void Boot_Usb_Reset(void)
{
  if (g_boot_usb_mounted != 0U)
  {
    (void)f_mount(NULL, (TCHAR const *)USBHPath, 0U);
  }
  g_boot_usb_scan_started = 0U;
  g_boot_usb_mounted = 0U;
}

bool Boot_Usb_IsMounted(void)
{
  return (g_boot_usb_mounted != 0U);
}

BootError Boot_Usb_BuildPath(const char *relative_path, char *buffer, size_t buffer_length)
{
  size_t drive_length;
  size_t relative_length;
  const char *path_ptr;

  if ((relative_path == NULL) || (buffer == NULL) || (buffer_length == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  drive_length = strlen(USBHPath);
  path_ptr = relative_path;
  if ((drive_length == 0U) || (drive_length >= buffer_length))
  {
    return BOOT_ERR_FS_MOUNT;
  }

  if ((USBHPath[drive_length - 1U] == '/') && (relative_path[0] == '/'))
  {
    path_ptr = &relative_path[1];
  }

  relative_length = strlen(path_ptr);
  if ((drive_length + relative_length + 1U) > buffer_length)
  {
    return BOOT_ERR_FILE_SIZE;
  }

  memset(buffer, 0, buffer_length);
  memcpy(buffer, USBHPath, drive_length);
  memcpy(buffer + drive_length, path_ptr, relative_length);
  return BOOT_ERR_NONE;
}

BootUsbScanResult Boot_Usb_PollForUpgradeMedia(uint32_t window_ms, BootError *error)
{
  FILINFO file_info;
  FRESULT fatfs_result;
  char path_buffer[64];
  uint32_t elapsed_ms;

  if (error != NULL)
  {
    *error = BOOT_ERR_NONE;
  }

  if (Appli_state == APPLICATION_DISCONNECT)
  {
    Boot_Usb_Reset();
    if (error != NULL)
    {
      *error = BOOT_ERR_USB_NOT_DETECTED;
    }
    return BOOT_USB_SCAN_NO_DEVICE;
  }

  if (g_boot_usb_scan_started == 0U)
  {
    g_boot_usb_start_tick = HAL_GetTick();
    g_boot_usb_scan_started = 1U;
  }

  elapsed_ms = HAL_GetTick() - g_boot_usb_start_tick;
  if (Appli_state != APPLICATION_READY)
  {
    if (elapsed_ms < window_ms)
    {
      return BOOT_USB_SCAN_WAITING;
    }

    if (error != NULL)
    {
      *error = BOOT_ERR_USB_NOT_DETECTED;
    }
    return BOOT_USB_SCAN_NO_DEVICE;
  }

  if (g_boot_usb_mounted == 0U)
  {
    fatfs_result = f_mount(&USBHFatFS, (TCHAR const *)USBHPath, 1U);
    if (fatfs_result != FR_OK)
    {
      if (error != NULL)
      {
        *error = BOOT_ERR_FS_MOUNT;
      }
      return BOOT_USB_SCAN_ERROR;
    }
    g_boot_usb_mounted = 1U;
  }

  if (Boot_Usb_BuildPath(BOOT_USB_BOOT_DIR, path_buffer, sizeof(path_buffer)) != BOOT_ERR_NONE)
  {
    if (error != NULL)
    {
      *error = BOOT_ERR_FS_MOUNT;
    }
    return BOOT_USB_SCAN_ERROR;
  }

  fatfs_result = f_stat(path_buffer, &file_info);
  if (fatfs_result != FR_OK)
  {
    if (error != NULL)
    {
      *error = BOOT_ERR_UPGRADE_DIR_MISSING;
    }
    return BOOT_USB_SCAN_MEDIA_INVALID;
  }

  if (Boot_Usb_BuildPath(BOOT_MANIFEST_PATH, path_buffer, sizeof(path_buffer)) != BOOT_ERR_NONE)
  {
    if (error != NULL)
    {
      *error = BOOT_ERR_FS_MOUNT;
    }
    return BOOT_USB_SCAN_ERROR;
  }

  fatfs_result = f_stat(path_buffer, &file_info);
  if (fatfs_result != FR_OK)
  {
    if (error != NULL)
    {
      *error = BOOT_ERR_MANIFEST_NOT_FOUND;
    }
    return BOOT_USB_SCAN_MEDIA_INVALID;
  }

  return BOOT_USB_SCAN_UPGRADE_READY;
}
