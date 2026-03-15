#include "boot_udisk_check.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_config.h"
#include "boot_usb.h"
#include "diskio.h"
#include "fatfs.h"
#include "ff.h"
#include "usb_host.h"

#define BOOT_UDISK_MAX_LINE_LENGTH         192U
#define BOOT_UDISK_MAX_SERIAL_LENGTH       64U
#define BOOT_UDISK_MAX_HASH_SOURCE_LENGTH  384U
#define BOOT_UDISK_HASH_HEX_LENGTH         64U

typedef struct
{
  uint32_t format_version;
  uint8_t has_format_version;

  char usb_vid[8];
  char usb_pid[8];
  char usb_serial[BOOT_UDISK_MAX_SERIAL_LENGTH];
  char capacity_bytes[24];
  char fat_bytes_per_sector[16];
  char fat_sectors_per_cluster[16];
  char fat_reserved_sectors[16];
  char fat_num_fats[16];
  char fat_size_32[16];
  char fat_root_cluster[16];
  char fat_volume_id[16];
  char fat_fs_type[16];
  char unique_code_sha256[BOOT_UDISK_HASH_HEX_LENGTH + 1U];
} BootUdiskCode;

typedef struct
{
  char usb_vid[8];
  char usb_pid[8];
  char usb_serial[BOOT_UDISK_MAX_SERIAL_LENGTH];

  uint64_t capacity_bytes;
  uint32_t fat_bytes_per_sector;
  uint32_t fat_sectors_per_cluster;
  uint32_t fat_reserved_sectors;
  uint32_t fat_num_fats;
  uint32_t fat_size_32;
  uint32_t fat_root_cluster;
  uint32_t fat_volume_id;
  char fat_fs_type[16];
} BootUdiskMediaInfo;

static uint16_t Boot_Udisk_ReadLe16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t Boot_Udisk_ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static char *Boot_Udisk_Trim(char *text)
{
  char *start = text;
  char *end;

  if (text == NULL)
  {
    return NULL;
  }

  while ((*start != '\0') && isspace((unsigned char)(*start)))
  {
    ++start;
  }

  end = start + strlen(start);
  while ((end > start) && isspace((unsigned char)(*(end - 1))))
  {
    --end;
  }

  *end = '\0';
  return start;
}

static void Boot_Udisk_ToUpper(char *text)
{
  if (text == NULL)
  {
    return;
  }

  while (*text != '\0')
  {
    *text = (char)toupper((unsigned char)(*text));
    ++text;
  }
}

static int Boot_Udisk_StrCaseEqual(const char *left, const char *right)
{
  unsigned char left_char;
  unsigned char right_char;

  if ((left == NULL) || (right == NULL))
  {
    return 0;
  }

  while ((*left != '\0') && (*right != '\0'))
  {
    left_char = (unsigned char)toupper((unsigned char)(*left));
    right_char = (unsigned char)toupper((unsigned char)(*right));
    if (left_char != right_char)
    {
      return 0;
    }
    ++left;
    ++right;
  }

  return (*left == '\0') && (*right == '\0');
}

static int Boot_Udisk_CopyString(char *dest, size_t dest_size, const char *src)
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

static int Boot_Udisk_IsHexString(const char *text)
{
  if ((text == NULL) || (*text == '\0'))
  {
    return 0;
  }

  while (*text != '\0')
  {
    if (!isxdigit((unsigned char)(*text)))
    {
      return 0;
    }
    ++text;
  }
  return 1;
}

static int Boot_Udisk_ParseU32(const char *text, uint32_t *value)
{
  char *end = NULL;
  unsigned long parsed;

  if ((text == NULL) || (value == NULL) || (*text == '\0'))
  {
    return -1;
  }

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if ((end == text) || (*end != '\0') || (errno != 0) || (parsed > 0xFFFFFFFFUL))
  {
    return -1;
  }

  *value = (uint32_t)parsed;
  return 0;
}

static int Boot_Udisk_ParseU64(const char *text, uint64_t *value)
{
  char *end = NULL;
  unsigned long long parsed;

  if ((text == NULL) || (value == NULL) || (*text == '\0'))
  {
    return -1;
  }

  errno = 0;
  parsed = strtoull(text, &end, 10);
  if ((end == text) || (*end != '\0') || (errno != 0))
  {
    return -1;
  }

  *value = (uint64_t)parsed;
  return 0;
}

static int Boot_Udisk_ParseHexU32(const char *text, uint32_t *value)
{
  char *end = NULL;
  unsigned long parsed;

  if ((text == NULL) || (value == NULL) || (*text == '\0'))
  {
    return -1;
  }

  errno = 0;
  parsed = strtoul(text, &end, 16);
  if ((end == text) || (*end != '\0') || (errno != 0) || (parsed > 0xFFFFFFFFUL))
  {
    return -1;
  }

  *value = (uint32_t)parsed;
  return 0;
}

static void Boot_Udisk_NormalizeFsType(char *fs_type)
{
  if (fs_type == NULL)
  {
    return;
  }

  (void)Boot_Udisk_Trim(fs_type);
  Boot_Udisk_ToUpper(fs_type);
}

static int Boot_Udisk_AssignField(BootUdiskCode *code, const char *key, const char *value)
{
  uint32_t format_version;

  if ((code == NULL) || (key == NULL) || (value == NULL))
  {
    return -1;
  }

  if (strcmp(key, "FormatVersion") == 0)
  {
    if (Boot_Udisk_ParseU32(value, &format_version) != 0)
    {
      return -1;
    }
    code->format_version = format_version;
    code->has_format_version = 1U;
    return 0;
  }
  if (strcmp(key, "USB_VID") == 0)
  {
    return Boot_Udisk_CopyString(code->usb_vid, sizeof(code->usb_vid), value);
  }
  if (strcmp(key, "USB_PID") == 0)
  {
    return Boot_Udisk_CopyString(code->usb_pid, sizeof(code->usb_pid), value);
  }
  if (strcmp(key, "USB_SERIAL") == 0)
  {
    return Boot_Udisk_CopyString(code->usb_serial, sizeof(code->usb_serial), value);
  }
  if (strcmp(key, "CAPACITY_BYTES") == 0)
  {
    return Boot_Udisk_CopyString(code->capacity_bytes, sizeof(code->capacity_bytes), value);
  }
  if (strcmp(key, "FAT_BYTES_PER_SECTOR") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_bytes_per_sector, sizeof(code->fat_bytes_per_sector), value);
  }
  if (strcmp(key, "FAT_SECTORS_PER_CLUSTER") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_sectors_per_cluster, sizeof(code->fat_sectors_per_cluster), value);
  }
  if (strcmp(key, "FAT_RESERVED_SECTORS") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_reserved_sectors, sizeof(code->fat_reserved_sectors), value);
  }
  if (strcmp(key, "FAT_NUM_FATS") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_num_fats, sizeof(code->fat_num_fats), value);
  }
  if (strcmp(key, "FAT_SIZE_32") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_size_32, sizeof(code->fat_size_32), value);
  }
  if (strcmp(key, "FAT_ROOT_CLUSTER") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_root_cluster, sizeof(code->fat_root_cluster), value);
  }
  if (strcmp(key, "FAT_VOLUME_ID") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_volume_id, sizeof(code->fat_volume_id), value);
  }
  if (strcmp(key, "FAT_FS_TYPE") == 0)
  {
    return Boot_Udisk_CopyString(code->fat_fs_type, sizeof(code->fat_fs_type), value);
  }
  if (strcmp(key, "UNIQUE_CODE_SHA256") == 0)
  {
    return Boot_Udisk_CopyString(code->unique_code_sha256, sizeof(code->unique_code_sha256), value);
  }

  return 0;
}

static BootError Boot_Udisk_ParseCodeFile(const char *path, BootUdiskCode *code)
{
  FIL file;
  FRESULT result;
  char line[BOOT_UDISK_MAX_LINE_LENGTH];
  uint32_t line_number = 0U;

  if ((path == NULL) || (code == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(code, 0, sizeof(*code));

  result = f_open(&file, path, FA_READ);
  if (result == FR_NO_FILE)
  {
    return BOOT_ERR_UDISK_CODE_NOT_FOUND;
  }
  if (result != FR_OK)
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  while (f_gets(line, (int)sizeof(line), &file) != NULL)
  {
    char *key;
    char *value;
    char *split;

    if ((line_number == 0U) &&
        ((uint8_t)line[0] == 0xEFU) &&
        ((uint8_t)line[1] == 0xBBU) &&
        ((uint8_t)line[2] == 0xBFU))
    {
      memmove(line, line + 3, strlen(line + 3) + 1U);
    }
    ++line_number;

    key = Boot_Udisk_Trim(line);
    if ((key == NULL) || (*key == '\0') || (*key == '#'))
    {
      continue;
    }

    split = strchr(key, '=');
    if (split == NULL)
    {
      continue;
    }

    *split = '\0';
    value = split + 1;
    key = Boot_Udisk_Trim(key);
    value = Boot_Udisk_Trim(value);
    if ((key == NULL) || (value == NULL))
    {
      (void)f_close(&file);
      return BOOT_ERR_UDISK_CODE_PARSE;
    }

    if (Boot_Udisk_AssignField(code, key, value) != 0)
    {
      (void)f_close(&file);
      return BOOT_ERR_UDISK_CODE_PARSE;
    }
  }

  (void)f_close(&file);

  if ((code->has_format_version == 0U) || (code->format_version != BOOT_UDISK_CHECK_FORMAT_VERSION))
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  if ((code->capacity_bytes[0] == '\0') ||
      (code->fat_bytes_per_sector[0] == '\0') ||
      (code->fat_sectors_per_cluster[0] == '\0') ||
      (code->fat_reserved_sectors[0] == '\0') ||
      (code->fat_num_fats[0] == '\0') ||
      (code->fat_size_32[0] == '\0') ||
      (code->fat_root_cluster[0] == '\0') ||
      (code->fat_volume_id[0] == '\0') ||
      (code->fat_fs_type[0] == '\0') ||
      (code->unique_code_sha256[0] == '\0'))
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  Boot_Udisk_ToUpper(code->usb_vid);
  Boot_Udisk_ToUpper(code->usb_pid);
  Boot_Udisk_ToUpper(code->fat_volume_id);
  Boot_Udisk_NormalizeFsType(code->fat_fs_type);
  Boot_Udisk_ToUpper(code->unique_code_sha256);

  if ((strlen(code->unique_code_sha256) != BOOT_UDISK_HASH_HEX_LENGTH) ||
      (Boot_Udisk_IsHexString(code->unique_code_sha256) == 0))
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Udisk_ReadMediaInfo(BootUdiskMediaInfo *info)
{
  MSC_LUNTypeDef lun_info;
  DRESULT disk_result;
  uint8_t boot_sector[512];
  uint16_t vid;
  uint16_t pid;
  const char *serial;

  if (info == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(info, 0, sizeof(*info));

  if (USBHFatFS.fs_type != FS_FAT32)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  if (USB_HOST_GetLunInfo(0U, &lun_info) != USBH_OK)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  info->capacity_bytes = ((uint64_t)lun_info.capacity.block_nbr + 1ULL) * (uint64_t)lun_info.capacity.block_size;

  disk_result = disk_read(USBHFatFS.drv, boot_sector, (DWORD)USBHFatFS.volbase, 1U);
  if (disk_result != RES_OK)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((boot_sector[510] != 0x55U) || (boot_sector[511] != 0xAAU))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  info->fat_bytes_per_sector = (uint32_t)Boot_Udisk_ReadLe16(&boot_sector[11]);
  info->fat_sectors_per_cluster = (uint32_t)boot_sector[13];
  info->fat_reserved_sectors = (uint32_t)Boot_Udisk_ReadLe16(&boot_sector[14]);
  info->fat_num_fats = (uint32_t)boot_sector[16];
  info->fat_size_32 = Boot_Udisk_ReadLe32(&boot_sector[36]);
  if (info->fat_size_32 == 0U)
  {
    info->fat_size_32 = (uint32_t)Boot_Udisk_ReadLe16(&boot_sector[22]);
  }
  info->fat_root_cluster = Boot_Udisk_ReadLe32(&boot_sector[44]);
  info->fat_volume_id = Boot_Udisk_ReadLe32(&boot_sector[67]);

  memset(info->fat_fs_type, 0, sizeof(info->fat_fs_type));
  memcpy(info->fat_fs_type, &boot_sector[82], 8U);
  Boot_Udisk_NormalizeFsType(info->fat_fs_type);
  if (Boot_Udisk_StrCaseEqual(info->fat_fs_type, "FAT32") == 0)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  vid = USB_HOST_GetVid();
  pid = USB_HOST_GetPid();
  (void)snprintf(info->usb_vid, sizeof(info->usb_vid), "%04X", (unsigned int)vid);
  (void)snprintf(info->usb_pid, sizeof(info->usb_pid), "%04X", (unsigned int)pid);

  serial = USB_HOST_GetSerial();
  if (serial == NULL)
  {
    info->usb_serial[0] = '\0';
  }
  else if (Boot_Udisk_CopyString(info->usb_serial, sizeof(info->usb_serial), serial) != 0)
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Udisk_CompareCodeAndMedia(const BootUdiskCode *code, const BootUdiskMediaInfo *info)
{
  uint64_t capacity_bytes;
  uint32_t number_value;
  uint32_t volume_id;

  if ((code == NULL) || (info == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if ((code->usb_vid[0] != '\0') && (Boot_Udisk_StrCaseEqual(code->usb_vid, info->usb_vid) == 0))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((code->usb_pid[0] != '\0') && (Boot_Udisk_StrCaseEqual(code->usb_pid, info->usb_pid) == 0))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((code->usb_serial[0] != '\0') && (strcmp(code->usb_serial, info->usb_serial) != 0))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU64(code->capacity_bytes, &capacity_bytes) != 0) ||
      (capacity_bytes != info->capacity_bytes))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_bytes_per_sector, &number_value) != 0) ||
      (number_value != info->fat_bytes_per_sector))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_sectors_per_cluster, &number_value) != 0) ||
      (number_value != info->fat_sectors_per_cluster))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_reserved_sectors, &number_value) != 0) ||
      (number_value != info->fat_reserved_sectors))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_num_fats, &number_value) != 0) ||
      (number_value != info->fat_num_fats))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_size_32, &number_value) != 0) ||
      (number_value != info->fat_size_32))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseU32(code->fat_root_cluster, &number_value) != 0) ||
      (number_value != info->fat_root_cluster))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if ((Boot_Udisk_ParseHexU32(code->fat_volume_id, &volume_id) != 0) ||
      (volume_id != info->fat_volume_id))
  {
    return BOOT_ERR_UDISK_INFO_MISMATCH;
  }

  if (Boot_Udisk_StrCaseEqual(code->fat_fs_type, "FAT32") == 0)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  if (Boot_Udisk_StrCaseEqual(info->fat_fs_type, "FAT32") == 0)
  {
    return BOOT_ERR_UDISK_NOT_FAT32;
  }

  return BOOT_ERR_NONE;
}

static uint32_t Boot_Udisk_Sha256RotateRight(uint32_t value, uint32_t shift)
{
  return (value >> shift) | (value << (32U - shift));
}

static uint32_t Boot_Udisk_Sha256Ch(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (~x & z);
}

static uint32_t Boot_Udisk_Sha256Maj(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t Boot_Udisk_Sha256BigSigma0(uint32_t x)
{
  return Boot_Udisk_Sha256RotateRight(x, 2U) ^
         Boot_Udisk_Sha256RotateRight(x, 13U) ^
         Boot_Udisk_Sha256RotateRight(x, 22U);
}

static uint32_t Boot_Udisk_Sha256BigSigma1(uint32_t x)
{
  return Boot_Udisk_Sha256RotateRight(x, 6U) ^
         Boot_Udisk_Sha256RotateRight(x, 11U) ^
         Boot_Udisk_Sha256RotateRight(x, 25U);
}

static uint32_t Boot_Udisk_Sha256SmallSigma0(uint32_t x)
{
  return Boot_Udisk_Sha256RotateRight(x, 7U) ^
         Boot_Udisk_Sha256RotateRight(x, 18U) ^
         (x >> 3U);
}

static uint32_t Boot_Udisk_Sha256SmallSigma1(uint32_t x)
{
  return Boot_Udisk_Sha256RotateRight(x, 17U) ^
         Boot_Udisk_Sha256RotateRight(x, 19U) ^
         (x >> 10U);
}

static void Boot_Udisk_Sha256Transform(uint32_t state[8], const uint8_t block[64])
{
  static const uint32_t k[64] =
  {
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
  };
  uint32_t w[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t temp1;
  uint32_t temp2;
  uint32_t i;

  for (i = 0U; i < 16U; ++i)
  {
    const uint32_t index = i * 4U;
    w[i] = ((uint32_t)block[index] << 24U) |
           ((uint32_t)block[index + 1U] << 16U) |
           ((uint32_t)block[index + 2U] << 8U) |
           ((uint32_t)block[index + 3U]);
  }

  for (i = 16U; i < 64U; ++i)
  {
    w[i] = Boot_Udisk_Sha256SmallSigma1(w[i - 2U]) +
           w[i - 7U] +
           Boot_Udisk_Sha256SmallSigma0(w[i - 15U]) +
           w[i - 16U];
  }

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];

  for (i = 0U; i < 64U; ++i)
  {
    temp1 = h + Boot_Udisk_Sha256BigSigma1(e) + Boot_Udisk_Sha256Ch(e, f, g) + k[i] + w[i];
    temp2 = Boot_Udisk_Sha256BigSigma0(a) + Boot_Udisk_Sha256Maj(a, b, c);

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

static void Boot_Udisk_Sha256(const uint8_t *data, size_t length, uint8_t output[32])
{
  uint32_t state[8] =
  {
    0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
    0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
  };
  uint8_t block[64];
  size_t full_blocks;
  size_t remainder;
  size_t offset;
  uint64_t bit_length;
  uint32_t i;

  if ((data == NULL) || (output == NULL))
  {
    return;
  }

  full_blocks = length / 64U;
  for (i = 0U; i < full_blocks; ++i)
  {
    Boot_Udisk_Sha256Transform(state, &data[i * 64U]);
  }

  remainder = length % 64U;
  memset(block, 0, sizeof(block));
  if (remainder > 0U)
  {
    memcpy(block, &data[full_blocks * 64U], remainder);
  }
  block[remainder] = 0x80U;

  if (remainder >= 56U)
  {
    Boot_Udisk_Sha256Transform(state, block);
    memset(block, 0, sizeof(block));
  }

  bit_length = (uint64_t)length * 8ULL;
  block[56] = (uint8_t)(bit_length >> 56U);
  block[57] = (uint8_t)(bit_length >> 48U);
  block[58] = (uint8_t)(bit_length >> 40U);
  block[59] = (uint8_t)(bit_length >> 32U);
  block[60] = (uint8_t)(bit_length >> 24U);
  block[61] = (uint8_t)(bit_length >> 16U);
  block[62] = (uint8_t)(bit_length >> 8U);
  block[63] = (uint8_t)(bit_length);
  Boot_Udisk_Sha256Transform(state, block);

  for (i = 0U; i < 8U; ++i)
  {
    offset = i * 4U;
    output[offset] = (uint8_t)(state[i] >> 24U);
    output[offset + 1U] = (uint8_t)(state[i] >> 16U);
    output[offset + 2U] = (uint8_t)(state[i] >> 8U);
    output[offset + 3U] = (uint8_t)(state[i]);
  }
}

static void Boot_Udisk_HexUpper(const uint8_t *input, size_t input_size, char *output, size_t output_size)
{
  static const char hex_chars[] = "0123456789ABCDEF";
  size_t i;

  if ((input == NULL) || (output == NULL) || (output_size < (input_size * 2U + 1U)))
  {
    return;
  }

  for (i = 0U; i < input_size; ++i)
  {
    output[i * 2U] = hex_chars[(input[i] >> 4U) & 0x0FU];
    output[i * 2U + 1U] = hex_chars[input[i] & 0x0FU];
  }
  output[input_size * 2U] = '\0';
}

static BootError Boot_Udisk_CheckHash(const BootUdiskCode *code, const BootUdiskMediaInfo *info)
{
  char hash_source[BOOT_UDISK_MAX_HASH_SOURCE_LENGTH];
  char volume_id_text[16];
  char hash_hex[BOOT_UDISK_HASH_HEX_LENGTH + 1U];
  uint8_t digest[32];
  const char *hash_vid;
  const char *hash_pid;
  const char *hash_serial;
  int length;

  if ((code == NULL) || (info == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  hash_vid = (code->usb_vid[0] != '\0') ? info->usb_vid : "";
  hash_pid = (code->usb_pid[0] != '\0') ? info->usb_pid : "";
  hash_serial = (code->usb_serial[0] != '\0') ? info->usb_serial : "";

  (void)snprintf(volume_id_text, sizeof(volume_id_text), "%08lX", (unsigned long)info->fat_volume_id);

  length = snprintf(
      hash_source,
      sizeof(hash_source),
      "USB_VID=%s|USB_PID=%s|USB_SERIAL=%s|CAPACITY_BYTES=%llu|FAT_BYTES_PER_SECTOR=%lu|"
      "FAT_SECTORS_PER_CLUSTER=%lu|FAT_RESERVED_SECTORS=%lu|FAT_NUM_FATS=%lu|FAT_SIZE_32=%lu|"
      "FAT_ROOT_CLUSTER=%lu|FAT_VOLUME_ID=%s|FAT_FS_TYPE=%s",
      hash_vid,
      hash_pid,
      hash_serial,
      (unsigned long long)info->capacity_bytes,
      (unsigned long)info->fat_bytes_per_sector,
      (unsigned long)info->fat_sectors_per_cluster,
      (unsigned long)info->fat_reserved_sectors,
      (unsigned long)info->fat_num_fats,
      (unsigned long)info->fat_size_32,
      (unsigned long)info->fat_root_cluster,
      volume_id_text,
      "FAT32");

  if ((length <= 0) || ((size_t)length >= sizeof(hash_source)))
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  Boot_Udisk_Sha256((const uint8_t *)(const void *)hash_source, (size_t)length, digest);
  Boot_Udisk_HexUpper(digest, sizeof(digest), hash_hex, sizeof(hash_hex));

  if (Boot_Udisk_StrCaseEqual(hash_hex, code->unique_code_sha256) == 0)
  {
    return BOOT_ERR_UDISK_HASH_MISMATCH;
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Udisk_Check(void)
{
  BootError error;
  char path_buffer[64];
  BootUdiskCode code;
  BootUdiskMediaInfo info;

  error = Boot_Usb_BuildPath(BOOT_UDISK_CHECK_PATH, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return BOOT_ERR_UDISK_CODE_PARSE;
  }

  error = Boot_Udisk_ParseCodeFile(path_buffer, &code);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  error = Boot_Udisk_ReadMediaInfo(&info);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  error = Boot_Udisk_CompareCodeAndMedia(&code, &info);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  return Boot_Udisk_CheckHash(&code, &info);
}
