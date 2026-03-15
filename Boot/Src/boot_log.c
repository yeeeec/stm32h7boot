#include "boot_log.h"

#include <stdio.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "logging.h"

static BootLogEntry g_boot_log_entries[BOOT_LOG_BUFFER_DEPTH];
static size_t g_boot_log_count;
static size_t g_boot_log_write_index;
static BootLogLevel g_boot_log_level = BOOT_LOG_INFO;
static BootError g_boot_last_error = BOOT_ERR_NONE;
static uint32_t g_boot_log_sequence;

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

void Boot_Log_Init(void)
{
  memset(g_boot_log_entries, 0, sizeof(g_boot_log_entries));
  g_boot_log_count = 0U;
  g_boot_log_write_index = 0U;
  g_boot_log_level = BOOT_LOG_INFO;
  g_boot_last_error = BOOT_ERR_NONE;
  g_boot_log_sequence = 0U;
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

  if ((message != NULL) && (message[0] != '\0'))
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
}

void Boot_Log_TryFlushToUsb(void)
{
  /* Bare-metal mode: no filesystem log sink. */
}

BootError Boot_Log_FlushToUsb(void)
{
  /* Bare-metal mode: keep API for compatibility, no-op. */
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
