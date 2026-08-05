/**
 * @file platform_reset_reason.h
 * @brief Reset-cause capture and query API.
 */
#ifndef PLATFORM_RESET_REASON_H
#define PLATFORM_RESET_REASON_H

/** Reset cause decoded from STM32 RCC reset flags. */
typedef enum
{
    PLATFORM_RESET_REASON_UNKNOWN = 0, /**< Cause could not be identified. */
    PLATFORM_RESET_REASON_POWER_ON, /**< BOR or POR reset. */
    PLATFORM_RESET_REASON_PIN, /**< NRST pin reset. */
    PLATFORM_RESET_REASON_SOFTWARE, /**< Software-requested reset. */
    PLATFORM_RESET_REASON_INDEPENDENT_WATCHDOG, /**< IWDG reset. */
    PLATFORM_RESET_REASON_WINDOW_WATCHDOG, /**< WWDG reset. */
    PLATFORM_RESET_REASON_LOW_POWER /**< Low-power reset. */
} platform_reset_reason_t;

/**
 * @brief Decode and store the current RCC reset flags.
 *
 * @note RCC reset flags are cleared after capture and are therefore only
 *       available through Platform_ResetReasonGet afterward.
 */
void Platform_ResetReasonCapture(void);

/**
 * @brief Return the most recently captured reset cause.
 *
 * @return Captured reset cause, or PLATFORM_RESET_REASON_UNKNOWN before capture.
 */
platform_reset_reason_t Platform_ResetReasonGet(void);

#endif
