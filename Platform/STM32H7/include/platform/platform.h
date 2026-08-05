/**
 * @file platform.h
 * @brief STM32H7 platform lifecycle entry points.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "firmware/status.h"

/**
 * @brief Initialize platform state and capture the reset reason.
 *
 * @return FIRMWARE_STATUS_OK on first initialization.
 * @return FIRMWARE_STATUS_INVALID_STATE if called more than once.
 */
firmware_status_t Platform_Init(void);

/**
 * @brief Run periodic platform maintenance.
 *
 * @note This entry point is currently a no-op and must not be assumed to block.
 */
void Platform_Process(void);

/**
 * @brief Query whether Platform_Init completed successfully.
 *
 * @return Nonzero after successful initialization; zero otherwise.
 */
int Platform_IsInitialized(void);

#endif
