/**
 * @file platform_reset.h
 * @brief STM32H7 system-reset entry point.
 */
#ifndef PLATFORM_RESET_H
#define PLATFORM_RESET_H

#if defined(__GNUC__)
#define PLATFORM_NORETURN __attribute__((noreturn))
#else
#define PLATFORM_NORETURN
#endif

/**
 * @brief Request a system reset and never return.
 *
 * @warning Pending maskable interrupts are disabled before the reset request.
 */
PLATFORM_NORETURN void Platform_Reset(void);

#endif
