/**
 * @file iwdg_backend.c
 * @brief PY32F003 Independent Watchdog (IWDG) backend implementation.
 *
 * Ref (PY32F003 RM):
 * - IWDG clocked by LSI ~32kHz
 * - KR keys: 0xCCCC start, 0x5555 unlock PR/RLR, 0xAAAA reload/feed
 * - PR divider: /4 /8 /16 /32 /64 /128 /256
 */

#include <stdint.h>

#include "gd32_mappings.h"
#include "platform/iwdg.h"
#include "platform/config.h"


static uint32_t g_configured_timeout_ms = 0;

/* ---------------- Public API ---------------- */

Plat_Status_t platform_iwdg_init(uint32_t timeout_ms) {
    g_configured_timeout_ms = timeout_ms;
#if PL_IS_IWDG
    fwdgt_config(g_configured_timeout_ms, FWDGT_PSC_DIV64);
    fwdgt_enable();
    fwdgt_counter_reload();
#endif
    return PLAT_OK;
}

void platform_iwdg_feed(void) {
#if PL_IS_IWDG
    fwdgt_counter_reload();
#endif
}

uint32_t platform_iwdg_get_configured_timeout_ms(void) {
    return g_configured_timeout_ms;
}
