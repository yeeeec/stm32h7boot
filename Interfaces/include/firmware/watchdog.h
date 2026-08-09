/**
 * @file watchdog.h
 * @brief Watchdog Refresh Provider Contract。
 */
#ifndef FIRMWARE_WATCHDOG_H
#define FIRMWARE_WATCHDOG_H

#include "firmware/status.h"

/** 在硬件 Timeout 窗口内刷新 Watchdog。 */
typedef firmware_status_t (*watchdog_refresh_fn)(void *context);

/** 由 Platform Provider 持有的 Watchdog 接口。 */
typedef struct
{
    void *context;
    watchdog_refresh_fn refresh;
} watchdog_t;

#endif
