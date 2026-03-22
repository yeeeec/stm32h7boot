#include "platform/plat_debug.h"
#include <stddef.h>

/** @brief 内部静态变量：存储注入进来的日志回调函数地址 */
static PlatLogCallback_t g_log_callback = NULL;

/**
 * @brief 注册日志回调函数（由上层 App 调用，注入实现）
 *
 * @param cb 日志回调函数指针
 */
void plat_debug_register_log_callback(PlatLogCallback_t cb)
{
    g_log_callback = cb;
}

/**
 * @brief 实际执行日志输出（若未注入回调则直接返回）
 *
 * @param level 日志级别
 * @param fmt   格式化字符串
 * @param ...   可变参数
 */
void plat_debug_log_message(PlatLogLevel_e level, const char *fmt, ...)
{
    /** 如果没有注入回调，直接返回，防止崩溃 */
    if (g_log_callback == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    /** 调用注入的回调函数 */
    g_log_callback(level, fmt, args);
    va_end(args);
}
