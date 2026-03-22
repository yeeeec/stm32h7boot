/**
 * @file logging.h
 * @brief Logging API and configuration macros.
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compatibility layer.
 *
 * New build macros from CMake:
 *   LOG_ENABLE (0/1)
 *   LOG_BACKEND_RTT (0/1)
 *   LOG_BACKEND_UART (0/1)
 */
#ifndef LOG_ENABLE
#ifndef PL_CONFIG_LOG_ENABLE
#define LOG_ENABLE 1
#else
#define LOG_ENABLE PL_CONFIG_LOG_ENABLE
#endif
#endif

/** @brief Log level: 0 NONE, 1 ERR, 2 WRN, 3 INF, 4 DBG. */
typedef enum {
    LOG_LVL_NONE  = 0,
    LOG_LVL_ERROR = 1,
    LOG_LVL_WARN  = 2,
    LOG_LVL_INFO  = 3,
    LOG_LVL_DEBUG = 4
} LogLevel_t;

#ifndef LOG_LEVEL
#ifndef PL_CONFIG_LOG_LEVEL
#define LOG_LEVEL LOG_LVL_DEBUG
#else
#define LOG_LEVEL PL_CONFIG_LOG_LEVEL
#endif
#endif

/** @brief Max size for a single log line. */
#define LOG_FMT_BUF_SIZE 128
/** @brief Async buffer size. */
#define LOG_RING_BUF_SIZE 2048


/* Macros */
#if (LOG_ENABLE == 1)

/** @name API
 *  @{ */
/**
 * @brief Initialize enabled logging backends.
 */
void logging_init_module(void);

/**
 * @brief Write a formatted log message.
 * @param level Log level.
 * @param tag   Optional tag string (may be NULL).
 * @param fmt   printf-style format string.
 */
void logging_write_message(LogLevel_t level, const char *tag, const char *fmt, ...);

/**
 * @brief Register a tick provider callback.
 * @param get_tick_fn Callback returning current tick count.
 */
void logging_register_tick_provider(uint32_t (*get_tick_fn)(void));
/** @} */


#if (LOG_LEVEL >= LOG_LVL_ERROR)
#define LOG_ERROR(tag, fmt, ...) logging_write_message(LOG_LVL_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(...) ((void) 0)
#endif

#if (LOG_LEVEL >= LOG_LVL_WARN)
#define LOG_WARN(tag, fmt, ...) logging_write_message(LOG_LVL_WARN, tag, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(...) ((void) 0)
#endif

#if (LOG_LEVEL >= LOG_LVL_INFO)
#define LOG_INFO(tag, fmt, ...) logging_write_message(LOG_LVL_INFO, tag, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(...) ((void) 0)
#endif

#if (LOG_LEVEL >= LOG_LVL_DEBUG)
#define LOG_DEBUG(tag, fmt, ...) logging_write_message(LOG_LVL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void) 0)
#endif

/** @brief Always prints when LOG_ENABLE=1. */
#define LOG_PANIC(tag, fmt, ...) logging_write_message(LOG_LVL_ERROR, tag, fmt, ##__VA_ARGS__)

#else

#define logging_init_module() ((void) 0)
#define LOG_ERROR(...)        ((void) 0)
#define LOG_WARN(...)         ((void) 0)
#define LOG_INFO(...)         ((void) 0)
#define LOG_DEBUG(...)        ((void) 0)
#define LOG_PANIC(...)        ((void) 0)

#define logging_register_tick_provider(...) ((void) 0)

#endif

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_H */
