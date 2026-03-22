/**
 * @file logging.c
 * @brief Logging frontend with selectable backends.
 */

#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

#include "log_backend_rtt.h"
#include "log_backend_uart.h"
#include <string.h>


/** ANSI colors (RTT terminal supports it). */
#define LOGGING_CLR_RED    "\033[31m"
#define LOGGING_CLR_YELLOW "\033[33m"
#define LOGGING_CLR_GREEN  "\033[32m"
#define LOGGING_CLR_CYAN   "\033[36m"
#define LOGGING_CLR_RESET  "\033[0m"

#if (LOG_ENABLE == 1)

/** @brief Registered tick provider callback (NULL if not set). */
static uint32_t (*g_tick_provider_get)(void) = NULL;
/**
 * @brief Register a tick provider callback.
 * @param get_tick_fn Callback returning current tick count.
 */

void logging_register_tick_provider(uint32_t (*get_tick_fn)(void)) {
    g_tick_provider_get = get_tick_fn;
}
/**
 * @brief Get current tick value from registered provider.
 * @return Tick value, or 0 if no provider is registered.
 */

static uint32_t logging_get_tick_value(void) {
    return g_tick_provider_get ? g_tick_provider_get() : 0;
}
/**
 * @brief Get ANSI color escape string for a log level.
 * @param level Log level.
 * @return Color escape string; empty string if level is unknown.
 */

static const char *logging_get_level_color(LogLevel_t level) {
    switch (level) {
        case LOG_LVL_ERROR:
            return LOGGING_CLR_RED;
        case LOG_LVL_WARN:
            return LOGGING_CLR_YELLOW;
        case LOG_LVL_INFO:
            return LOGGING_CLR_GREEN;
        case LOG_LVL_DEBUG:
            return LOGGING_CLR_CYAN;
        default:
            return "";
    }
}

static void logging_format_time(uint32_t tick_ms, char *out, size_t out_size) {
    uint32_t ms   = tick_ms % 1000;
    uint32_t sec  = (tick_ms / 1000) % 60;
    uint32_t min  = (tick_ms / 60000) % 60;
    uint32_t hour = (tick_ms / 3600000);

    snprintf(out, out_size, "%02lu:%02lu:%02lu:%03lu", (unsigned long) hour, (unsigned long) min,
             (unsigned long) sec, (unsigned long) ms);
}


/**
 * @brief Initialize enabled logging backends.
 */

void logging_init_module(void) {
#if LOG_BACKEND_RTT
    log_backend_rtt_init();
#endif
#if LOG_BACKEND_UART
    log_backend_uart_init();
#endif
}
/**
 * @brief Output log buffer to all enabled backends.
 * @param buf Pointer to buffer.
 * @param len Number of bytes to output.
 */

static void logging_output_all_backends(const char *buf, uint16_t len) {
#if LOG_BACKEND_RTT
    log_backend_rtt_output(buf, len);
#endif
#if LOG_BACKEND_UART
    log_backend_uart_output(buf, len);
#endif
}
/**
 * @brief Write a formatted log message.
 * @param level Log level.
 * @param tag   Optional tag string (may be NULL).
 * @param fmt   printf-style format string.
 */

void logging_write_message(LogLevel_t level, const char *tag, const char *fmt, ...) {
    char buf[LOG_FMT_BUF_SIZE + 16];
    int len = 0;

    if ((int) level > (int) LOG_LEVEL)
        return;

    const char *color = logging_get_level_color(level);
    uint32_t tick     = logging_get_tick_value();
    char time_str[16]; // hh:mm:ss:ms

    logging_format_time(tick, time_str, sizeof(time_str));

    len += snprintf(buf, sizeof(buf), "%s[%s][%s] ", color, time_str, tag ? tag : "SYS");
    if (len < 0)
        return;
    if (len >= (int) sizeof(buf))
        len = (int) sizeof(buf) - 1;

    va_list args;
    va_start(args, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - (size_t) len, fmt, args);
    va_end(args);

    if (len < 0)
        return;
    if (len > (int) sizeof(buf) - 8)
        len = (int) sizeof(buf) - 8;

    if (color[0] != '\0') {
        len += snprintf(buf + len, sizeof(buf) - (size_t) len, "%s\r\n", LOGGING_CLR_RESET);
    } else {
        len += snprintf(buf + len, sizeof(buf) - (size_t) len, "\r\n");
    }

    if (len < 0)
        return;
    logging_output_all_backends(buf, (uint16_t) len);
}

#else /* LOG_ENABLE == 0 */

#endif
