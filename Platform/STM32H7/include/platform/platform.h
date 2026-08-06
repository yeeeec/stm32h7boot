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
 * The call is bounded and refreshes the independent watchdog at the platform
 * maintenance interval. A stalled upper layer therefore stops refreshing it.
 *
 * @return FIRMWARE_STATUS_OK when maintenance succeeds.
 * @return FIRMWARE_STATUS_INVALID_STATE before Platform_Init.
 * @return FIRMWARE_STATUS_IO_ERROR if watchdog refresh fails.
 */
firmware_status_t Platform_Process(void);

/**
 * @brief Query whether Platform_Init completed successfully.
 *
 * @return Nonzero after successful initialization; zero otherwise.
 */
int Platform_IsInitialized(void);

#endif
