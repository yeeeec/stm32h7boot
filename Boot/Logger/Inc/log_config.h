#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

#include "logging.h"

/* master switch */
#define LOG_ENABLE 1

/* global level */
#define LOG_LEVEL LOG_LVL_DEBUG

/* features */
#define LOG_ENABLE_COLOR 1
#define LOG_ENABLE_TIMESTAMP 0

// /* backends */
// #define LOG_BACKEND_RTT 1
// #define LOG_BACKEND_UART 0

/* module levels */

#define LOG_LEVEL_BOOT LOG_LVL_INFO
#define LOG_LEVEL_USB  LOG_LVL_DEBUG
#define LOG_LEVEL_NET  LOG_LVL_WARN
#define LOG_LEVEL_FS   LOG_LVL_INFO

#endif