/**
 * @file system_reset.h
 * @brief 与 Platform 无关的系统 Reset Contract。
 */
#ifndef FIRMWARE_SYSTEM_RESET_H
#define FIRMWARE_SYSTEM_RESET_H

/**
 * @brief 请求立即执行全系统 Reset。
 *
 * Reset 请求成功后不会返回。若控制权返回，表示 Reset 未执行，属于
 * Provider Contract 违规。
 */
typedef void (*system_reset_request_fn)(void *context);

/** 携带 Provider-owned context 的系统 Reset 接口。 */
typedef struct
{
    void *context;
    system_reset_request_fn request;
} system_reset_t;

#endif
