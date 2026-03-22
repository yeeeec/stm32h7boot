/** platform/include/platform/iwdg.h */
#ifndef PLATFORM_IWDG_H
#define PLATFORM_IWDG_H

#include <stdint.h>

#include "definitions.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init & start IWDG with timeout (ms).
 *
 * Notes:
 * - IWDG runs from LSI ~32kHz and cannot be stopped once started.
 * - This API enables LSI, configures prescaler/reload, starts IWDG, then feeds once.
 *
 * @param timeout_ms Desired timeout in milliseconds.
 * @return PLAT_OK or error code.
 */
Plat_Status_t platform_iwdg_init(uint32_t timeout_ms);

/**
 * @brief Refresh (feed) IWDG to prevent reset.
 */
void platform_iwdg_feed(void);

/**
 * @brief Get the timeout actually configured (ms, rounded).
 *        Useful because prescaler/reload quantize the time.
 */
uint32_t platform_iwdg_get_configured_timeout_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_IWDG_H */
