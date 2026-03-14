#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LVL_NONE  = 0,
    LOG_LVL_ERROR = 1,
    LOG_LVL_WARN  = 2,
    LOG_LVL_INFO  = 3,
    LOG_LVL_DEBUG = 4
} LogLevel_t;

#define LOG_FMT_BUF_SIZE 128

#if LOG_ENABLE

void logging_init(void);
void logging_register_tick_provider(uint32_t (*fn)(void));

void logging_write(LogLevel_t level,
                   const char *tag,
                   const char *fmt,
                   ...);

void logging_hexdump(LogLevel_t level,
                     const char *tag,
                     const void *data,
                     uint32_t len);

/* normal logs */

#define LOG_ERROR(tag, fmt, ...) logging_write(LOG_LVL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  logging_write(LOG_LVL_WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  logging_write(LOG_LVL_INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) logging_write(LOG_LVL_DEBUG, tag, fmt, ##__VA_ARGS__)

/* hexdump */

#define LOG_HEXDUMP(tag, data, len) \
logging_hexdump(LOG_LVL_DEBUG, tag, data, len)

/* log once */

#define LOG_ONCE(tag, fmt, ...) \
do { \
    static int done; \
    if (!done) { \
        done = 1; \
        LOG_INFO(tag, fmt, ##__VA_ARGS__); \
    } \
} while(0)

/* every N */

#define LOG_EVERY_N(n, tag, fmt, ...) \
do { \
    static uint32_t cnt; \
    cnt++; \
    if ((cnt % (n)) == 0) \
        LOG_INFO(tag, fmt, ##__VA_ARGS__); \
} while(0)

#else

#define logging_init()
#define LOG_ERROR(...)
#define LOG_WARN(...)
#define LOG_INFO(...)
#define LOG_DEBUG(...)
#define LOG_HEXDUMP(...)

#endif

#ifdef __cplusplus
}
#endif

#endif