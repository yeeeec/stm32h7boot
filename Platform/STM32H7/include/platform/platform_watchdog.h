/**
 * @file platform_watchdog.h
 * @brief STM32H7 independent-watchdog service.
 */
#ifndef PLATFORM_WATCHDOG_H
#define PLATFORM_WATCHDOG_H

#include "firmware/status.h"

/**
 * @brief Refresh the configured independent watchdog.
 *
 * @return FIRMWARE_STATUS_OK when the HAL accepts the refresh.
 * @return FIRMWARE_STATUS_IO_ERROR when the HAL refresh fails.
 */
firmware_status_t Platform_WatchdogRefresh(void);

#endif
