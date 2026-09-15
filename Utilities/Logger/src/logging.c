/**
 * @file logging.c
 * @brief Timestamped log formatting and dependency-backed output.
 */
#include "logging.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "logging_setup.h"

#define LOGGING_LINE_SIZE 192U

#define LOGGING_COLOR_RED    "\033[31m"
#define LOGGING_COLOR_YELLOW "\033[33m"
#define LOGGING_COLOR_GREEN  "\033[32m"
#define LOGGING_COLOR_CYAN   "\033[36m"
#define LOGGING_COLOR_RESET  "\033[0m"

static log_output_port_t logging_port;
static int logging_initialized;

/* Compatibility storage for the former Composition-only two-port API. */
static log_sink_t legacy_sink;
static system_clock_t legacy_clock;

static firmware_status_t LegacyWrite(void *context, const uint8_t *data, size_t size)
{
    (void) context;
    return legacy_sink.write(legacy_sink.context, data, size);
}

static uint32_t LegacyNowMs(void *context)
{
    (void) context;
    return legacy_clock.now_ms(legacy_clock.context);
}

firmware_status_t Logging_Init(const log_output_port_t *port)
{
    if ((port == NULL) || (port->write == NULL) || (port->now_ms == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (logging_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    logging_port        = *port;
    logging_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Logging_SetOutputPort(const log_output_port_t *port)
{
    if ((port == NULL) || (port->write == NULL) || (port->now_ms == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (logging_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    logging_port = *port;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Logging_Configure(const log_sink_t *sink, const system_clock_t *clock)
{
    if ((sink == NULL) || (sink->write == NULL) || (clock == NULL) || (clock->now_ms == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (logging_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    legacy_sink  = *sink;
    legacy_clock = *clock;
    {
        const log_output_port_t port = {NULL, LegacyWrite, LegacyNowMs};
        return Logging_Init(&port);
    }
}

#if FIRMWARE_LOG_ENABLE

static const char *LevelName(logging_level_t level)
{
    switch (level)
    {
        case LOGGING_LEVEL_ERROR:
            return "ERR";
        case LOGGING_LEVEL_WARN:
            return "WRN";
        case LOGGING_LEVEL_INFO:
            return "INF";
        case LOGGING_LEVEL_DEBUG:
            return "DBG";
        default:
            return "UNK";
    }
}

static const char *LevelColor(logging_level_t level)
{
#if FIRMWARE_LOG_USE_COLOR
    switch (level)
    {
        case LOGGING_LEVEL_ERROR:
            return LOGGING_COLOR_RED;
        case LOGGING_LEVEL_WARN:
            return LOGGING_COLOR_YELLOW;
        case LOGGING_LEVEL_INFO:
            return LOGGING_COLOR_GREEN;
        case LOGGING_LEVEL_DEBUG:
            return LOGGING_COLOR_CYAN;
        default:
            return "";
    }
#else
    (void) level;
    return "";
#endif
}

static size_t FormattedLength(int result, size_t capacity)
{
    if ((result <= 0) || (capacity == 0U))
    {
        return 0U;
    }
    if ((size_t) result >= capacity)
    {
        return capacity - 1U;
    }
    return (size_t) result;
}

void Logging_Write(logging_level_t level, const char *tag, const char *format, ...)
{
    char line[LOGGING_LINE_SIZE];
    char time[16];
    const char *color;
    const char *suffix;
    size_t suffix_length;
    size_t used;
    size_t body_capacity;
    uint32_t tick_ms;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t milliseconds;
    int result;
    va_list arguments;

    if ((logging_initialized == 0) || (format == NULL) || ((int) level < FIRMWARE_LOG_LEVEL_ERROR) ||
        ((int) level > FIRMWARE_LOG_LEVEL_DEBUG))
    {
        return;
    }

    tick_ms      = logging_port.now_ms(logging_port.context);
    hours        = tick_ms / 3600000U;
    minutes      = (tick_ms / 60000U) % 60U;
    seconds      = (tick_ms / 1000U) % 60U;
    milliseconds = tick_ms % 1000U;
    (void) snprintf(time, sizeof(time), "%02lu:%02lu:%02lu.%03lu", (unsigned long) hours,
                    (unsigned long) minutes, (unsigned long) seconds, (unsigned long) milliseconds);

    color         = LevelColor(level);
    suffix        = (color[0] == '\0') ? "\r\n" : LOGGING_COLOR_RESET "\r\n";
    suffix_length = strlen(suffix);

    result = snprintf(line, sizeof(line), "%s[%s][%s][%s] ", color, time, LevelName(level),
                      (tag == NULL) ? "SYS" : tag);
    if (result < 0)
    {
        return;
    }
    used = FormattedLength(result, sizeof(line));
    if (used > (sizeof(line) - suffix_length - 1U))
    {
        used = sizeof(line) - suffix_length - 1U;
    }

    body_capacity = sizeof(line) - used - suffix_length;
    va_start(arguments, format);
    result = vsnprintf(&line[used], body_capacity, format, arguments);
    va_end(arguments);
    if (result < 0)
    {
        return;
    }
    used += FormattedLength(result, body_capacity);

    memcpy(&line[used], suffix, suffix_length);
    used += suffix_length;
    (void) logging_port.write(logging_port.context, (const uint8_t *) line, used);
}

#endif
