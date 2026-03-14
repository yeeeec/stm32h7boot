#include "log_backend_rtt.h"
#include "logging.h"

#if LOG_ENABLE && LOG_BACKEND_RTT

#include "SEGGER_RTT.h"

#ifndef LOG_RTT_CHANNEL
#define LOG_RTT_CHANNEL 0
#endif

void log_backend_rtt_init(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_ConfigUpBuffer(LOG_RTT_CHANNEL, "Terminal", NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

void log_backend_rtt_output(const char *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    (void)SEGGER_RTT_Write(LOG_RTT_CHANNEL, data, len);
}

#else

void log_backend_rtt_init(void) {}
void log_backend_rtt_output(const char *data, uint16_t len) { (void)data; (void)len; }

#endif
