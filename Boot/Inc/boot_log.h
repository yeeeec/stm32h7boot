#ifndef BOOT_LOG_H
#define BOOT_LOG_H

#include <stddef.h>

#include "boot_error.h"
#include "boot_types.h"
#include "logging.h"

void Boot_Log_Init(void);

/* Legacy API kept for compatibility with older call sites. */
void Boot_Log_Write(BootLogLevel level, BootError error, const char *message);

#define BOOT_LOGD(fmt, ...) logging_write(LOG_LVL_DEBUG, "BOOT", fmt, ##__VA_ARGS__)
#define BOOT_LOGI(fmt, ...) logging_write(LOG_LVL_INFO,  "BOOT", fmt, ##__VA_ARGS__)
#define BOOT_LOGW(fmt, ...) logging_write(LOG_LVL_WARN,  "BOOT", fmt, ##__VA_ARGS__)
#define BOOT_LOGE(fmt, ...) logging_write(LOG_LVL_ERROR, "BOOT", fmt, ##__VA_ARGS__)

#define BOOT_LOGW_ERR(error, fmt, ...) \
  BOOT_LOGW("%s: " fmt, Boot_ErrorToString((BootError)(error)), ##__VA_ARGS__)

#define BOOT_LOGE_ERR(error, fmt, ...) \
  BOOT_LOGE("%s: " fmt, Boot_ErrorToString((BootError)(error)), ##__VA_ARGS__)

#endif
