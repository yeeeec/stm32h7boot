/**
 * @file logging.h
 * @brief Hardware-independent formatted logging macros.
 *
 * Logging is configured once by the Composition layer. Callers only select a
 * severity and tag; they do not depend on the UART, HAL, or clock provider.
 */
#ifndef FIRMWARE_LOGGING_H
#define FIRMWARE_LOGGING_H

#ifndef FIRMWARE_LOG_ENABLE
#define FIRMWARE_LOG_ENABLE 1
#endif

#define FIRMWARE_LOG_LEVEL_NONE  0
#define FIRMWARE_LOG_LEVEL_ERROR 1
#define FIRMWARE_LOG_LEVEL_WARN  2
#define FIRMWARE_LOG_LEVEL_INFO  3
#define FIRMWARE_LOG_LEVEL_DEBUG 4

#ifndef FIRMWARE_LOG_LEVEL
#define FIRMWARE_LOG_LEVEL FIRMWARE_LOG_LEVEL_DEBUG
#endif

typedef enum
{
    LOGGING_LEVEL_ERROR = FIRMWARE_LOG_LEVEL_ERROR,
    LOGGING_LEVEL_WARN = FIRMWARE_LOG_LEVEL_WARN,
    LOGGING_LEVEL_INFO = FIRMWARE_LOG_LEVEL_INFO,
    LOGGING_LEVEL_DEBUG = FIRMWARE_LOG_LEVEL_DEBUG
} logging_level_t;

#if defined(__GNUC__) || defined(__clang__)
#define LOGGING_PRINTF_FORMAT(format_index, argument_index) \
    __attribute__((format(printf, format_index, argument_index)))
#else
#define LOGGING_PRINTF_FORMAT(format_index, argument_index)
#endif

/**
 * Format one log line and send it to the configured sink.
 * Messages written before configuration are discarded.
 */
void Logging_Write(
    logging_level_t level,
    const char *tag,
    const char *format,
    ...) LOGGING_PRINTF_FORMAT(3, 4);

#if FIRMWARE_LOG_ENABLE

#if FIRMWARE_LOG_LEVEL >= FIRMWARE_LOG_LEVEL_ERROR
#define LOG_ERROR(tag, format, ...) \
    Logging_Write(LOGGING_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if FIRMWARE_LOG_LEVEL >= FIRMWARE_LOG_LEVEL_WARN
#define LOG_WARN(tag, format, ...) \
    Logging_Write(LOGGING_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if FIRMWARE_LOG_LEVEL >= FIRMWARE_LOG_LEVEL_INFO
#define LOG_INFO(tag, format, ...) \
    Logging_Write(LOGGING_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if FIRMWARE_LOG_LEVEL >= FIRMWARE_LOG_LEVEL_DEBUG
#define LOG_DEBUG(tag, format, ...) \
    Logging_Write(LOGGING_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

/* Panic messages bypass the selected level but still require logging enabled. */
#define LOG_PANIC(tag, format, ...) \
    Logging_Write(LOGGING_LEVEL_ERROR, tag, format, ##__VA_ARGS__)

#else

#define LOG_DISABLED(level, tag, format, ...)                         \
    do                                                               \
    {                                                                \
        if (0)                                                       \
        {                                                            \
            Logging_Write(level, tag, format, ##__VA_ARGS__);       \
        }                                                            \
    } while (0)

#define LOG_ERROR(tag, format, ...) \
    LOG_DISABLED(LOGGING_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...) \
    LOG_DISABLED(LOGGING_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...) \
    LOG_DISABLED(LOGGING_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOG_DEBUG(tag, format, ...) \
    LOG_DISABLED(LOGGING_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOG_PANIC(tag, format, ...) \
    LOG_DISABLED(LOGGING_LEVEL_ERROR, tag, format, ##__VA_ARGS__)

#endif

#endif
