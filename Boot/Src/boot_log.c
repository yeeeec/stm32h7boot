#include "boot_log.h"

#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "boot_usb.h"
#include "logging.h"

static BootLogEntry g_boot_log_entries[BOOT_LOG_BUFFER_DEPTH];
static size_t g_boot_log_count;
static size_t g_boot_log_write_index;
static BootLogLevel g_boot_log_level = BOOT_LOG_INFO;
static BootError g_boot_last_error = BOOT_ERR_NONE;
static uint32_t g_boot_log_sequence;
static uint32_t g_boot_log_flushed_sequence;

static const char *Boot_Log_LevelToString(BootLogLevel level)
{
  switch (level)
  {
    case BOOT_LOG_INFO:
      return "INFO";
    case BOOT_LOG_WARN:
      return "WARN";
    case BOOT_LOG_ERROR:
      return "ERROR";
    case BOOT_LOG_RESULT:
      return "RESULT";
    default:
      return "UNK";
  }
}

static LogLevel_t Boot_Log_ToFrontendLevel(BootLogLevel level)
{
  switch (level)
  {
    case BOOT_LOG_ERROR:
      return LOG_LVL_ERROR;
    case BOOT_LOG_WARN:
      return LOG_LVL_WARN;
    case BOOT_LOG_INFO:
      return LOG_LVL_INFO;
    case BOOT_LOG_RESULT:
      return LOG_LVL_INFO;
    default:
      return LOG_LVL_DEBUG;
  }
}

static uint32_t Boot_Log_GetOldestSequence(void)
{
  if (g_boot_log_count == 0U)
  {
    return 0U;
  }

  if (g_boot_log_sequence > g_boot_log_count)
  {
    return g_boot_log_sequence - (uint32_t)g_boot_log_count + 1U;
  }

  return 1U;
}

static const BootLogEntry *Boot_Log_GetEntryBySequence(uint32_t sequence)
{
  uint32_t oldest_sequence;

  if ((sequence == 0U) || (g_boot_log_count == 0U))
  {
    return NULL;
  }

  oldest_sequence = Boot_Log_GetOldestSequence();
  if ((sequence < oldest_sequence) || (sequence > g_boot_log_sequence))
  {
    return NULL;
  }

  return &g_boot_log_entries[(sequence - 1U) % BOOT_LOG_BUFFER_DEPTH];
}

static void Boot_Log_FormatLine(const BootLogEntry *entry, char *line, size_t line_size)
{
  if ((entry == NULL) || (line == NULL) || (line_size == 0U))
  {
    return;
  }

  (void)snprintf(line,
                 line_size,
                 "[%08lu][%08lu][%s][%s] %s\r\n",
                 (unsigned long)entry->sequence,
                 (unsigned long)entry->timestamp_ms,
                 Boot_Log_LevelToString(entry->level),
                 Boot_ErrorToString((BootError)entry->error_code),
                 entry->message);
}

static void Boot_Log_OutputRealtime(const BootLogEntry *entry)
{
  char line[160];

  if (entry == NULL)
  {
    return;
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[%08lu][%08lu][%s][%s] %s",
                 (unsigned long)entry->sequence,
                 (unsigned long)entry->timestamp_ms,
                 Boot_Log_LevelToString(entry->level),
                 Boot_ErrorToString((BootError)entry->error_code),
                 entry->message);

  logging_write(Boot_Log_ToFrontendLevel(entry->level), "BOOT", "%s", line);
}

static BootError Boot_Log_CheckDirectory(const char *relative_path)
{
  FILINFO file_info;
  BootError error;
  char path_buffer[64];

  error = Boot_Usb_BuildPath(relative_path, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  return (f_stat(path_buffer, &file_info) == FR_OK) ? BOOT_ERR_NONE : BOOT_ERR_UPGRADE_DIR_MISSING;
}

static BootError Boot_Log_EnsureDirectory(const char *relative_path)
{
  FILINFO file_info;
  FRESULT fatfs_result;
  BootError error;
  char path_buffer[64];

  error = Boot_Usb_BuildPath(relative_path, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  fatfs_result = f_stat(path_buffer, &file_info);
  if (fatfs_result == FR_OK)
  {
    return BOOT_ERR_NONE;
  }

  fatfs_result = f_mkdir(path_buffer);
  if ((fatfs_result != FR_OK) && (fatfs_result != FR_EXIST))
  {
    return BOOT_ERR_FILE_WRITE;
  }

  return BOOT_ERR_NONE;
}

void Boot_Log_Init(void)
{
  memset(g_boot_log_entries, 0, sizeof(g_boot_log_entries));
  g_boot_log_count = 0U;
  g_boot_log_write_index = 0U;
  g_boot_log_level = BOOT_LOG_INFO;
  g_boot_last_error = BOOT_ERR_NONE;
  g_boot_log_sequence = 0U;
  g_boot_log_flushed_sequence = 0U;
  logging_register_tick_provider(HAL_GetTick);
  logging_init();
}

void Boot_Log_SetLevel(BootLogLevel level)
{
  g_boot_log_level = level;
}

void Boot_Log_Write(BootLogLevel level, BootError error, const char *message)
{
  BootLogEntry *entry;
  if (level < g_boot_log_level)
  {
    return;
  }

  entry = &g_boot_log_entries[g_boot_log_write_index];
  memset(entry, 0, sizeof(*entry));
  entry->sequence = ++g_boot_log_sequence;
  entry->timestamp_ms = HAL_GetTick();
  entry->level = level;
  entry->error_code = (uint32_t)error;
  if (message != NULL)
  {
    strncpy(entry->message, message, sizeof(entry->message) - 1U);
  }
  else
  {
    strncpy(entry->message, Boot_ErrorToString(error), sizeof(entry->message) - 1U);
  }

  g_boot_last_error = error;
  if (g_boot_log_count < BOOT_LOG_BUFFER_DEPTH)
  {
    ++g_boot_log_count;
  }
  g_boot_log_write_index = (g_boot_log_write_index + 1U) % BOOT_LOG_BUFFER_DEPTH;

  Boot_Log_OutputRealtime(entry);
  Boot_Log_TryFlushToUsb();
}

void Boot_Log_TryFlushToUsb(void)
{
  if (Boot_Usb_IsMounted())
  {
    (void)Boot_Log_FlushToUsb();
  }
}

BootError Boot_Log_FlushToUsb(void)
{
  BootError error;
  FRESULT fatfs_result;
  FIL log_file;
  UINT bytes_written;
  char path_buffer[64];
  char line_buffer[160];
  uint32_t sequence;
  uint32_t oldest_sequence;
  const BootLogEntry *entry;

  if (!Boot_Usb_IsMounted())
  {
    return BOOT_ERR_USB_NOT_DETECTED;
  }

  if (g_boot_log_sequence == g_boot_log_flushed_sequence)
  {
    return BOOT_ERR_NONE;
  }

  error = Boot_Log_CheckDirectory(BOOT_USB_BOOT_DIR);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  error = Boot_Log_EnsureDirectory(BOOT_LOG_DIR_PATH);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  error = Boot_Usb_BuildPath(BOOT_LOG_FILE_PATH, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  fatfs_result = f_open(&log_file, path_buffer, FA_OPEN_ALWAYS | FA_WRITE);
  if (fatfs_result != FR_OK)
  {
    return BOOT_ERR_FILE_WRITE;
  }

  fatfs_result = f_lseek(&log_file, f_size(&log_file));
  if (fatfs_result != FR_OK)
  {
    (void)f_close(&log_file);
    return BOOT_ERR_FILE_WRITE;
  }

  oldest_sequence = Boot_Log_GetOldestSequence();
  sequence = g_boot_log_flushed_sequence + 1U;
  if ((sequence == 0U) || (sequence < oldest_sequence))
  {
    sequence = oldest_sequence;
  }

  for (; sequence <= g_boot_log_sequence; ++sequence)
  {
    entry = Boot_Log_GetEntryBySequence(sequence);
    if (entry == NULL)
    {
      continue;
    }

    Boot_Log_FormatLine(entry, line_buffer, sizeof(line_buffer));
    bytes_written = 0U;
    fatfs_result = f_write(&log_file, line_buffer, (UINT)strlen(line_buffer), &bytes_written);
    if ((fatfs_result != FR_OK) || (bytes_written != strlen(line_buffer)))
    {
      (void)f_close(&log_file);
      return BOOT_ERR_FILE_WRITE;
    }
  }

  (void)f_close(&log_file);
  g_boot_log_flushed_sequence = g_boot_log_sequence;
  return BOOT_ERR_NONE;
}

BootError Boot_Log_GetLastError(void)
{
  return g_boot_last_error;
}

const BootLogEntry *Boot_Log_GetEntries(size_t *count)
{
  if (count != NULL)
  {
    *count = g_boot_log_count;
  }
  return g_boot_log_entries;
}
