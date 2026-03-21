#include "log_backend_rtt.h"
#include "logging.h"

#if LOG_ENABLE && LOG_BACKEND_RTT

#include "SEGGER_RTT.h"
#include "stm32h7xx_hal.h"

#ifndef LOG_RTT_CHANNEL
#define LOG_RTT_CHANNEL 0
#endif

static void log_backend_rtt_flush_cache(void)
{
#if defined(SCB_CCR_DC_Msk)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache();
        __DSB();
        __ISB();
    }
#endif
}

void log_backend_rtt_init(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_ConfigUpBuffer(LOG_RTT_CHANNEL, "Terminal", NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    log_backend_rtt_flush_cache();
}

void log_backend_rtt_output(const char *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    (void)SEGGER_RTT_Write(LOG_RTT_CHANNEL, data, len);
    log_backend_rtt_flush_cache();
}

#else

void log_backend_rtt_init(void) {}
void log_backend_rtt_output(const char *data, uint16_t len) { (void)data; (void)len; }

#endif
