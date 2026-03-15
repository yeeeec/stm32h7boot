#include "boot_log.h"

#include <string.h>

#include "stm32h7xx_hal.h"

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

void Boot_Log_Init(void)
{
  logging_register_tick_provider(HAL_GetTick);
  logging_init();
}

void Boot_Log_Write(BootLogLevel level, BootError error, const char *message)
{
  const char *error_text;

  if ((message != NULL) && (message[0] != '\0'))
  {
    if (error == BOOT_ERR_NONE)
    {
      logging_write(Boot_Log_ToFrontendLevel(level), "BOOT", "%s", message);
      return;
    }

    error_text = Boot_ErrorToString(error);
    if (strcmp(message, error_text) == 0)
    {
      logging_write(Boot_Log_ToFrontendLevel(level), "BOOT", "%s", error_text);
      return;
    }

    logging_write(Boot_Log_ToFrontendLevel(level), "BOOT", "%s: %s", error_text, message);
    return;
  }

  logging_write(Boot_Log_ToFrontendLevel(level), "BOOT", "%s", Boot_ErrorToString(error));
}
