#include "logging.h"
#include "log_config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "log_backend_rtt.h"
#include "log_backend_uart.h"

/* colors */

#define C_RED   "\033[31m"
#define C_YEL   "\033[33m"
#define C_GRN   "\033[32m"
#define C_BLU   "\033[34m"
#define C_RST   "\033[0m"

#if LOG_ENABLE

static uint32_t (*tick_get)(void);

/* ================= backend ================= */

static void backend_write(const char *buf, uint16_t len)
{
#if LOG_BACKEND_RTT
    log_backend_rtt_output(buf, len);
#endif

#if LOG_BACKEND_UART
    log_backend_uart_output(buf, len);
#endif
}

/* ================= tick ================= */

void logging_register_tick_provider(uint32_t (*fn)(void))
{
    tick_get = fn;
}

#if LOG_ENABLE_TIMESTAMP
static uint32_t get_tick(void)
{
    if (tick_get)
        return tick_get();

    return 0;
}
#endif

/* ================= level ================= */

static const char *lvl_str(LogLevel_t l)
{
    switch(l)
    {
        case LOG_LVL_ERROR: return "ERROR";
        case LOG_LVL_WARN:  return "WARN ";
        case LOG_LVL_INFO:  return "INFO ";
        case LOG_LVL_DEBUG: return "DEBUG";
        default: return "LOG  ";
    }
}

static const char *lvl_color(LogLevel_t l)
{
#if LOG_ENABLE_COLOR
    switch(l)
    {
        case LOG_LVL_ERROR: return C_RED;
        case LOG_LVL_WARN:  return C_YEL;
        case LOG_LVL_INFO:  return C_GRN;
        case LOG_LVL_DEBUG: return C_BLU;
        default: return "";
    }
#else
    (void)l;
    return "";
#endif
}

/* ================= module level ================= */

static LogLevel_t module_lvl(const char *tag)
{
    if (!tag)
        return LOG_LEVEL;

    if (!strcmp(tag,"BOOT"))
        return LOG_LEVEL_BOOT;

    if (!strcmp(tag,"USB"))
        return LOG_LEVEL_USB;

    if (!strcmp(tag,"NET"))
        return LOG_LEVEL_NET;

    if (!strcmp(tag,"FS"))
        return LOG_LEVEL_FS;

    return LOG_LEVEL;
}

/* ================= time ================= */


#if LOG_ENABLE_TIMESTAMP
static void fmt_time(uint32_t ms, char *out)
{
    uint32_t s = ms / 1000;
    uint32_t m = s / 60;
    uint32_t h = m / 60;

    ms %= 1000;
    s %= 60;
    m %= 60;

    sprintf(out,"%02lu:%02lu:%02lu:%03lu",
        (unsigned long)h,
        (unsigned long)m,
        (unsigned long)s,
        (unsigned long)ms);
}
#endif

/* ================= init ================= */

void logging_init(void)
{
#if LOG_BACKEND_RTT
    log_backend_rtt_init();
#endif

#if LOG_BACKEND_UART
    log_backend_uart_init();
#endif
}

/* ================= write ================= */

void logging_write(LogLevel_t lvl,
                   const char *tag,
                   const char *fmt,
                   ...)
{
    if (lvl > module_lvl(tag))
        return;

    char buf[LOG_FMT_BUF_SIZE+64];

#if LOG_ENABLE_TIMESTAMP
    char time[16];
#endif

    int len;

#if LOG_ENABLE_TIMESTAMP
    fmt_time(get_tick(),time);
#endif

#if LOG_ENABLE_TIMESTAMP && LOG_ENABLE_COLOR

    len = snprintf(buf,sizeof(buf),
    "[%s][%s%s%s][%s] ",
    time,
    lvl_color(lvl),
    lvl_str(lvl),
    C_RST,
    tag?tag:"SYS");

#elif LOG_ENABLE_TIMESTAMP

    len = snprintf(buf,sizeof(buf),
    "[%s][%s][%s] ",
    time,
    lvl_str(lvl),
    tag?tag:"SYS");

#elif LOG_ENABLE_COLOR

    len = snprintf(buf,sizeof(buf),
    "[%s%s%s][%s] ",
    lvl_color(lvl),
    lvl_str(lvl),
    C_RST,
    tag?tag:"SYS");

#else

    len = snprintf(buf,sizeof(buf),
    "[%s][%s] ",
    lvl_str(lvl),
    tag?tag:"SYS");

#endif

    if (len<0) return;

    va_list ap;

    va_start(ap,fmt);

    len += vsnprintf(buf+len,
                     sizeof(buf)-len,
                     fmt,
                     ap);

    va_end(ap);

    if (len > (int)sizeof(buf)-3)
        len = sizeof(buf)-3;

    buf[len++]='\r';
    buf[len++]='\n';
    buf[len]=0;

    backend_write(buf,len);
}

/* ================= hexdump ================= */

void logging_hexdump(LogLevel_t lvl,
                     const char *tag,
                     const void *data,
                     uint32_t len)
{
    if (lvl > module_lvl(tag))
        return;

    const uint8_t *p=data;

    char line[96];
    int pos;

    for(uint32_t i=0;i<len;i+=16)
    {
        pos=sprintf(line,"%04lx: ",
            (unsigned long)i);

        for(uint32_t j=0;j<16 && i+j<len;j++)
        {
            pos+=sprintf(line+pos,
                "%02X ",
                p[i+j]);
        }

        logging_write(lvl,tag,"%s",line);
    }
}

#endif