/**
 * @file sha256.h
 * @brief Incremental, allocation-free SHA-256 implementation.
 */
#ifndef CRYPTO_SHA256_H
#define CRYPTO_SHA256_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define CRYPTO_SHA256_DIGEST_SIZE 32U

/** Mutable SHA-256 state. */
typedef struct
{
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    uint32_t buffer_size;
} sha256_context_t;

/* STM32H7APP-compatible name. The implementation remains the allocation-free
 * SHA-256 engine above; no mbedTLS or RTOS dependency is introduced. */
typedef sha256_context_t crypto_sha256_context_t;

/** Reset a context for a new digest. */
firmware_status_t Sha256_Reset(sha256_context_t *context);

/** Consume bytes without retaining the caller buffer. */
firmware_status_t Sha256_Update(sha256_context_t *context, const void *data, size_t size);

/** Finalize into a 32-byte digest without modifying the source context. */
firmware_status_t Sha256_Finish(const sha256_context_t *context,
                                uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);

firmware_status_t Crypto_Sha256Init(crypto_sha256_context_t *context);
firmware_status_t Crypto_Sha256Update(crypto_sha256_context_t *context,
                                      const uint8_t *data, size_t size);
firmware_status_t Crypto_Sha256Finish(crypto_sha256_context_t *context,
                                      uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);
void Crypto_Sha256Abort(crypto_sha256_context_t *context);
firmware_status_t Crypto_Sha256(const uint8_t *data, size_t size,
                                uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);

#endif
