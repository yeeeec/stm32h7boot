/**
 * @file hash.h
 * @brief 增量密码 Hash Provider Contract。
 */
#ifndef FIRMWARE_HASH_H
#define FIRMWARE_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define FIRMWARE_SHA256_DIGEST_SIZE 32U

/** 开始新的 Hash 计算并丢弃之前的 digest 状态。 */
typedef firmware_status_t (*hash_provider_reset_fn)(void *context);
/** 同步消费输入；Provider 不保留该 Buffer。 */
typedef firmware_status_t (*hash_provider_update_fn)(
    void *context,
    const void *data,
    size_t size);
/** 完成计算，并写入恰好 FIRMWARE_SHA256_DIGEST_SIZE 字节。 */
typedef firmware_status_t (*hash_provider_finish_fn)(
    void *context,
    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE]);

/**
 * @brief 增量 SHA-256 Provider。
 *
 * 一个 Provider 持有一个可变 context。reset()、update() 和 finish() 必须按
 * 顺序调用，调用者不得交错执行两次计算。
 */
typedef struct
{
    void *context;
    hash_provider_reset_fn reset;
    hash_provider_update_fn update;
    hash_provider_finish_fn finish;
} hash_provider_t;

#endif
