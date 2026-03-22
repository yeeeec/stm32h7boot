#ifndef BOOT_USB_H
#define BOOT_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_error.h"

typedef enum
{
  BOOT_USB_SCAN_WAITING = 0,
  BOOT_USB_SCAN_NO_DEVICE,
  BOOT_USB_SCAN_UPGRADE_READY,
  BOOT_USB_SCAN_MEDIA_INVALID,
  BOOT_USB_SCAN_ERROR
} BootUsbScanResult;

void Boot_Usb_Init(void);
void Boot_Usb_Reset(void);
bool Boot_Usb_IsMounted(void);
BootError Boot_Usb_BuildPath(const char *relative_path, char *buffer, size_t buffer_length);
BootUsbScanResult Boot_Usb_PollForUpgradeMedia(uint32_t window_ms, BootError *error);

#endif
