#include "boot_udisk_check.h"

#include <string.h>

#include "boot_config.h"
#include "boot_usb.h"
#include "diskio.h"
#include "fatfs.h"
#include "ff.h"
#include "usb_host.h"

#define BOOT_UDISK_CHECK_CODE_BYTES 4U
#define BOOT_UDISK_SECTOR_BYTES     512U
#define BOOT_UDISK_BOOT_SIG_OFFSET  510U
#define BOOT_UDISK_VOL_ID_OFFSET    67U

static uint32_t Boot_Udisk_ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

#if (BOOT_UDISK_CHECK_CODE_LITTLE_ENDIAN != 1U)
static uint32_t Boot_Udisk_ReadBe32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24U) |
         ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) |
         (uint32_t)data[3];
}
#endif

static char Boot_Udisk_ToUpperAscii(char ch)
{
  if ((ch >= 'a') && (ch <= 'z'))
  {
    return (char)(ch - ('a' - 'A'));
  }
  return ch;
}

static uint8_t Boot_Udisk_IsDigitAscii(char ch)
{
  return ((ch >= '0') && (ch <= '9')) ? 1U : 0U;
}

static uint8_t Boot_Udisk_StartsWithIgnoreCase(const char *text, const char *prefix)
{
  if ((text == NULL) || (prefix == NULL))
  {
    return 0U;
  }

  while (*prefix != '\0')
  {
    if (*text == '\0')
    {
      return 0U;
    }
    if (Boot_Udisk_ToUpperAscii(*text) != Boot_Udisk_ToUpperAscii(*prefix))
    {
      return 0U;
    }
    ++text;
    ++prefix;
  }

  return 1U;
}

static const char *Boot_Udisk_NormalizeSerialStart(const char *serial)
{
  if (serial == NULL)
  {
    return "";
  }

  /* Keep behavior aligned with Windows PNP path extraction in the check script. */
  if (Boot_Udisk_StartsWithIgnoreCase(serial, "MSFT30") != 0U)
  {
    return serial + 6;
  }

  return serial;
}

static size_t Boot_Udisk_NormalizeSerialLength(const char *serial)
{
  size_t length;
  size_t tail;

  if (serial == NULL)
  {
    return 0U;
  }

  length = strlen(serial);
  tail = length;

  while ((tail > 0U) && (Boot_Udisk_IsDigitAscii(serial[tail - 1U]) != 0U))
  {
    --tail;
  }

  if ((tail < length) && (tail > 0U) && (serial[tail - 1U] == '&'))
  {
    return tail - 1U;
  }

  return length;
}

static uint32_t Boot_Udisk_CalculateSerialSum(const char *serial)
{
  uint32_t sum = 0U;
  size_t index;
  size_t length;
  const char *normalized;

  normalized = Boot_Udisk_NormalizeSerialStart(serial);
  length = Boot_Udisk_NormalizeSerialLength(normalized);

  for (index = 0U; index < length; ++index)
  {
    sum += (uint32_t)(uint8_t)normalized[index];
  }

  return sum;
}

static uint32_t Boot_Udisk_CalculateCheckCode(uint32_t volume_id, const char *serial)
{
  uint32_t sum = BOOT_UDISK_CHECK_FEATURE_CODE;

  sum += volume_id;
  sum += Boot_Udisk_CalculateSerialSum(serial);

  return sum;
}

static BootError Boot_Udisk_ReadExpectedCheckCode(uint32_t *check_code)
{
  FIL file;
  FRESULT result;
  UINT bytes_read = 0U;
  char path_buffer[64];
  uint8_t raw_code[BOOT_UDISK_CHECK_CODE_BYTES];
  BootError path_error;

  if (check_code == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  path_error = Boot_Usb_BuildPath(BOOT_UDISK_CHECK_PATH, path_buffer, sizeof(path_buffer));
  if (path_error != BOOT_ERR_NONE)
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  result = f_open(&file, path_buffer, FA_READ);
  if (result == FR_NO_FILE)
  {
    return BOOT_ERR_UDISK_CODE_NOT_FOUND;
  }
  if (result != FR_OK)
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  if (f_size(&file) != BOOT_UDISK_CHECK_CODE_BYTES)
  {
    (void)f_close(&file);
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  result = f_read(&file, raw_code, BOOT_UDISK_CHECK_CODE_BYTES, &bytes_read);
  (void)f_close(&file);
  if ((result != FR_OK) || (bytes_read != BOOT_UDISK_CHECK_CODE_BYTES))
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

#if (BOOT_UDISK_CHECK_CODE_LITTLE_ENDIAN == 1U)
  *check_code = Boot_Udisk_ReadLe32(raw_code);
#else
  *check_code = Boot_Udisk_ReadBe32(raw_code);
#endif

  return BOOT_ERR_NONE;
}

static BootError Boot_Udisk_ReadVolumeId(uint32_t *volume_id)
{
  uint8_t boot_sector[BOOT_UDISK_SECTOR_BYTES];

  if (volume_id == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (USBHFatFS.fs_type != FS_FAT32)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  if (disk_read(USBHFatFS.drv, boot_sector, (DWORD)USBHFatFS.volbase, 1U) != RES_OK)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((boot_sector[BOOT_UDISK_BOOT_SIG_OFFSET] != 0x55U) ||
      (boot_sector[BOOT_UDISK_BOOT_SIG_OFFSET + 1U] != 0xAAU))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  *volume_id = Boot_Udisk_ReadLe32(&boot_sector[BOOT_UDISK_VOL_ID_OFFSET]);
  return BOOT_ERR_NONE;
}

BootError Boot_Udisk_Check(void)
{
  uint32_t expected_code;
  uint32_t local_code;
  uint32_t volume_id;
  BootError error;

  error = Boot_Udisk_ReadExpectedCheckCode(&expected_code);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  error = Boot_Udisk_ReadVolumeId(&volume_id);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  local_code = Boot_Udisk_CalculateCheckCode(volume_id, USB_HOST_GetSerial());
  if (local_code != expected_code)
  {
    return BOOT_ERR_UDISK_HASH_MISMATCH;
  }

  return BOOT_ERR_NONE;
}
