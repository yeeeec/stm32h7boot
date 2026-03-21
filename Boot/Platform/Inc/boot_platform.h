#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#include "boot_error.h"

#define BOOT_PLATFORM_USB_PATH_MAX       64U
#define BOOT_PLATFORM_USB_ID_LENGTH      5U
#define BOOT_PLATFORM_USB_SERIAL_LENGTH  64U

typedef enum
{
  BOOT_PLATFORM_USB_STATE_IDLE = 0,
  BOOT_PLATFORM_USB_STATE_DISCONNECTED,
  BOOT_PLATFORM_USB_STATE_ENUMERATING,
  BOOT_PLATFORM_USB_STATE_READY
} BootPlatformUsbState;

typedef struct
{
  FIL handle;
  uint8_t is_open;
} BootPlatformFile;

typedef struct
{
  char usb_vid[BOOT_PLATFORM_USB_ID_LENGTH];
  char usb_pid[BOOT_PLATFORM_USB_ID_LENGTH];
  char usb_serial[BOOT_PLATFORM_USB_SERIAL_LENGTH];
  uint32_t volume_id;
  uint8_t is_fat32;
} BootPlatformUsbIdentity;

uint32_t Boot_Platform_GetTickMs(void);

BootPlatformUsbState Boot_Platform_GetUsbState(void);
BootError Boot_Platform_UsbMount(void);
void Boot_Platform_UsbUnmount(void);

BootError Boot_Platform_BuildUsbPath(const char *relative_path, char *buffer, size_t buffer_length);
bool Boot_Platform_FileExists(const char *relative_path);
BootError Boot_Platform_FileOpenRead(const char *relative_path, BootPlatformFile *file);
BootError Boot_Platform_FileRead(BootPlatformFile *file, void *buffer, uint32_t size, uint32_t *bytes_read);
BootError Boot_Platform_FileSeek(BootPlatformFile *file, uint32_t offset);
uint32_t Boot_Platform_FileSize(const BootPlatformFile *file);
void Boot_Platform_FileClose(BootPlatformFile *file);

BootError Boot_Platform_ReadUsbIdentity(BootPlatformUsbIdentity *identity);

BootError Boot_Platform_FlashUnlock(void);
void Boot_Platform_FlashLock(void);
void Boot_Platform_FlashClearAllFlags(void);
BootError Boot_Platform_FlashEraseSectors(uint32_t bank, uint32_t start_sector, uint32_t count);
BootError Boot_Platform_FlashProgramFlashWord(uint32_t address, const void *data);
void Boot_Platform_FlashRefreshCache(void);

void Boot_Platform_PrepareForJump(void);
void Boot_Platform_FeedWatchdog(void);

#endif
