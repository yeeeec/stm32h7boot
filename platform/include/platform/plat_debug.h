#ifndef PLAT_DEBUG_H
#define PLAT_DEBUG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 平台日志级别定义
 */
typedef enum {
    PLAT_LOG_LEVEL_INFO,
    PLAT_LOG_LEVEL_WARN,
    PLAT_LOG_LEVEL_ERROR
} PlatLogLevel_e;

/**
 * @brief 日志回调函数类型
 *
 * @param level 日志级别
 * @param fmt   格式化字符串
 * @param args  可变参数列表
 */
typedef void (*PlatLogCallback_t)(PlatLogLevel_e level, const char *fmt, va_list args);

/**
 * @brief 注册日志回调函数（由上层注入实现）
 *
 * @param cb 日志回调函数指针
 */
void plat_debug_register_log_callback(PlatLogCallback_t cb);

/**
 * @brief 平台内部日志输出接口
 *
 * @param level 日志级别
 * @param fmt   格式化字符串
 * @param ...   可变参数
 */
void plat_debug_log_message(PlatLogLevel_e level, const char *fmt, ...);

/** @brief INFO 级别日志输出宏 */
#define PLAT_DEBUG_LOG_INFO(fmt, ...)  plat_debug_log_message(PLAT_LOG_LEVEL_INFO, (fmt), ##__VA_ARGS__)
/** @brief WARN 级别日志输出宏 */
#define PLAT_DEBUG_LOG_WARN(fmt, ...)  plat_debug_log_message(PLAT_LOG_LEVEL_WARN, (fmt), ##__VA_ARGS__)
/** @brief ERROR 级别日志输出宏 */
#define PLAT_DEBUG_LOG_ERR(fmt, ...)   plat_debug_log_message(PLAT_LOG_LEVEL_ERROR, (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* PLAT_DEBUG_H */
