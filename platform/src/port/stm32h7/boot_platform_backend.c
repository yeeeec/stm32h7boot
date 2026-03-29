#include "platform/boot_platform.h"

#include <stdio.h>
#include <string.h>

#include "diskio.h"
#include "fatfs.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"

extern ApplicationTypeDef Appli_state;

static uint32_t Boot_Platform_ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static int Boot_Platform_CopyString(char *dest, size_t dest_size, const char *src)
{
  size_t src_length;

  if ((dest == NULL) || (src == NULL) || (dest_size == 0U))
  {
    return -1;
  }

  src_length = strlen(src);
  if (src_length >= dest_size)
  {
    return -1;
  }

  memcpy(dest, src, src_length + 1U);
  return 0;
}

static void Boot_Platform_DisableInterrupts(void)
{
  __disable_irq();

  for (uint32_t index = 0U; index < 8U; ++index)
  {
    NVIC->ICER[index] = 0xFFFFFFFFUL;
    NVIC->ICPR[index] = 0xFFFFFFFFUL;
  }
}

uint32_t Boot_Platform_GetTickMs(void)
{
  return HAL_GetTick();
}

BootPlatformUsbState Boot_Platform_GetUsbState(void)
{
  switch (Appli_state)
  {
    case APPLICATION_DISCONNECT:
      return BOOT_PLATFORM_USB_STATE_DISCONNECTED;

    case APPLICATION_READY:
      return BOOT_PLATFORM_USB_STATE_READY;

    case APPLICATION_IDLE:
      return BOOT_PLATFORM_USB_STATE_IDLE;

    case APPLICATION_START:
    default:
      return BOOT_PLATFORM_USB_STATE_ENUMERATING;
  }
}

BootError Boot_Platform_UsbMount(void)
{
  return (f_mount(&USBHFatFS, (TCHAR const *)USBHPath, 1U) == FR_OK) ? BOOT_ERR_NONE : BOOT_ERR_FS_MOUNT;
}

void Boot_Platform_UsbUnmount(void)
{
  (void)f_mount(NULL, (TCHAR const *)USBHPath, 0U);
}

BootError Boot_Platform_BuildUsbPath(const char *relative_path, char *buffer, size_t buffer_length)
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

bool Boot_Platform_FileExists(const char *relative_path)
{
  FILINFO file_info;
  char path_buffer[BOOT_PLATFORM_USB_PATH_MAX];

  if (Boot_Platform_BuildUsbPath(relative_path, path_buffer, sizeof(path_buffer)) != BOOT_ERR_NONE)
  {
    return false;
  }

  return (f_stat(path_buffer, &file_info) == FR_OK);
}

BootError Boot_Platform_FileOpenRead(const char *relative_path, BootPlatformFile *file)
{
  FRESULT fatfs_result;
  char path_buffer[BOOT_PLATFORM_USB_PATH_MAX];
  BootError error;

  if (file == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(file, 0, sizeof(*file));
  error = Boot_Platform_BuildUsbPath(relative_path, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  fatfs_result = f_open(&file->handle, path_buffer, FA_READ);
  if ((fatfs_result == FR_NO_FILE) || (fatfs_result == FR_NO_PATH))
  {
    return BOOT_ERR_FILE_MISSING;
  }
  if (fatfs_result != FR_OK)
  {
    return BOOT_ERR_FS_MOUNT;
  }

  file->is_open = 1U;
  return BOOT_ERR_NONE;
}

BootError Boot_Platform_FileRead(BootPlatformFile *file, void *buffer, uint32_t size, uint32_t *bytes_read)
{
  FRESULT fatfs_result;
  UINT actual_read;

  if ((file == NULL) || (buffer == NULL) || (bytes_read == NULL) || (file->is_open == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  *bytes_read = 0U;
  if (size == 0U)
  {
    return BOOT_ERR_NONE;
  }

  actual_read = 0U;
  fatfs_result = f_read(&file->handle, buffer, size, &actual_read);
  *bytes_read = (uint32_t)actual_read;
  return (fatfs_result == FR_OK) ? BOOT_ERR_NONE : BOOT_ERR_FS_MOUNT;
}

BootError Boot_Platform_FileSeek(BootPlatformFile *file, uint32_t offset)
{
  if ((file == NULL) || (file->is_open == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  return (f_lseek(&file->handle, offset) == FR_OK) ? BOOT_ERR_NONE : BOOT_ERR_FS_MOUNT;
}

uint32_t Boot_Platform_FileSize(const BootPlatformFile *file)
{
  if ((file == NULL) || (file->is_open == 0U))
  {
    return 0U;
  }

  return (uint32_t)f_size(&file->handle);
}

void Boot_Platform_FileClose(BootPlatformFile *file)
{
  if ((file == NULL) || (file->is_open == 0U))
  {
    return;
  }

  (void)f_close(&file->handle);
  file->is_open = 0U;
}

BootError Boot_Platform_ReadUsbIdentity(BootPlatformUsbIdentity *identity)
{
  DRESULT disk_result;
  uint8_t boot_sector[512];
  const char *serial;

  if (identity == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(identity, 0, sizeof(*identity));

  if (USBHFatFS.fs_type != FS_FAT32)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  disk_result = disk_read(USBHFatFS.drv, boot_sector, (DWORD)USBHFatFS.volbase, 1U);
  if (disk_result != RES_OK)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((boot_sector[510] != 0x55U) || (boot_sector[511] != 0xAAU))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((char)boot_sector[82] != 'F' ||
      (char)boot_sector[83] != 'A' ||
      (char)boot_sector[84] != 'T' ||
      (char)boot_sector[85] != '3' ||
      (char)boot_sector[86] != '2')
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  (void)snprintf(identity->usb_vid, sizeof(identity->usb_vid), "%04X", (unsigned int)USB_HOST_GetVid());
  (void)snprintf(identity->usb_pid, sizeof(identity->usb_pid), "%04X", (unsigned int)USB_HOST_GetPid());
  if ((identity->usb_vid[0] == '0') && (identity->usb_vid[1] == '0') &&
      (identity->usb_vid[2] == '0') && (identity->usb_vid[3] == '0') &&
      (identity->usb_pid[0] == '0') && (identity->usb_pid[1] == '0') &&
      (identity->usb_pid[2] == '0') && (identity->usb_pid[3] == '0'))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  serial = USB_HOST_GetSerial();
  if ((serial == NULL) || (*serial == '\0'))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }
  if (Boot_Platform_CopyString(identity->usb_serial, sizeof(identity->usb_serial), serial) != 0)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  identity->volume_id = Boot_Platform_ReadLe32(&boot_sector[67]);
  identity->is_fat32 = 1U;
  return BOOT_ERR_NONE;
}

BootError Boot_Platform_FlashUnlock(void)
{
  return (HAL_FLASH_Unlock() == HAL_OK) ? BOOT_ERR_NONE : BOOT_ERR_FLASH_WRITE;
}

void Boot_Platform_FlashLock(void)
{
  (void)HAL_FLASH_Lock();
}

void Boot_Platform_FlashClearAllFlags(void)
{
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_EOP_BANK1);
#if defined(DUAL_BANK)
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2 | FLASH_FLAG_EOP_BANK2);
#endif
}

BootError Boot_Platform_FlashEraseSectors(uint32_t bank, uint32_t start_sector, uint32_t count)
{
  FLASH_EraseInitTypeDef erase_init;
  uint32_t sector_error = 0xFFFFFFFFUL;

  if (count == 0U)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(&erase_init, 0, sizeof(erase_init));
  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Banks = bank;
  erase_init.Sector = start_sector;
  erase_init.NbSectors = count;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  return (HAL_FLASHEx_Erase(&erase_init, &sector_error) == HAL_OK) ? BOOT_ERR_NONE : BOOT_ERR_FLASH_ERASE;
}

BootError Boot_Platform_FlashProgramFlashWord(uint32_t address, const void *data)
{
  if (data == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                            address,
                            (uint32_t)(uintptr_t)data) == HAL_OK) ? BOOT_ERR_NONE : BOOT_ERR_FLASH_WRITE;
}

void Boot_Platform_FlashRefreshCache(void)
{
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanInvalidateDCache();
  }

  if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
  {
    SCB_InvalidateICache();
  }
}

uint32_t Boot_Platform_ReadResetFlags(void)
{
  return RCC->RSR;
}

void Boot_Platform_ClearResetFlags(void)
{
  SET_BIT(RCC->RSR, RCC_RSR_RMVF);
  __DSB();
  __ISB();
}

void Boot_Platform_PrepareForJump(void)
{
  Boot_Platform_DisableInterrupts();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

#if defined(SCB_CCR_DC_Msk)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_DisableDCache();
  }
#endif
#if defined(SCB_CCR_IC_Msk)
  if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
  {
    SCB_DisableICache();
  }
#endif

  __DSB();
  __ISB();
}

void Boot_Platform_FeedWatchdog(void)
{
}
