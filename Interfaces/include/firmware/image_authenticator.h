/**
 * @file image_authenticator.h
 * @brief 增量 SHA-256 与签名 Digest 校验 Contract。
 */
#ifndef FIRMWARE_IMAGE_AUTHENTICATOR_H
#define FIRMWARE_IMAGE_AUTHENTICATOR_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define IMAGE_AUTHENTICATOR_SHA256_SIZE 32U

/** 开始对一个镜像 Hash；丢弃之前的 Hash 状态。 */
typedef firmware_status_t (*image_authenticator_hash_reset_fn)(void *context);
/** 同步消费镜像字节，不保留调用者的 Buffer。 */
typedef firmware_status_t (*image_authenticator_hash_update_fn)(
    void *context,
    const void *data,
    size_t size);
/** 将 Digest 完成并写入调用者提供的 32 字节 Buffer。 */
typedef firmware_status_t (*image_authenticator_hash_finish_fn)(
    void *context,
    uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE]);
/** 校验 Digest 签名；未知 Key 或编码必须 Fail-closed。 */
typedef firmware_status_t (*image_authenticator_verify_signature_fn)(
    void *context,
    const char *key_id,
    const uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE],
    const uint8_t *signature,
    size_t signature_size);

/**
 * @brief Hash 与签名校验接口。
 *
 * Provider 持有一个可变 Hash context。调用者持有输入和输出 Buffer。校验只
 * 根据完成后的 Digest 与签名做决定；未知 Key 标识或编码必须 Fail-closed。
 */
typedef struct
{
    void *context;
    image_authenticator_hash_reset_fn hash_reset;
    image_authenticator_hash_update_fn hash_update;
    image_authenticator_hash_finish_fn hash_finish;
    image_authenticator_verify_signature_fn verify_signature;
} image_authenticator_t;

#endif
