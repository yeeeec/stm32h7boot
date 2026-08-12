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

/** Reset a context for a new digest. */
firmware_status_t Sha256_Reset(sha256_context_t *context);

/** Consume bytes without retaining the caller buffer. */
firmware_status_t Sha256_Update(sha256_context_t *context, const void *data, size_t size);

/** Finalize into a 32-byte digest without modifying the source context. */
firmware_status_t Sha256_Finish(const sha256_context_t *context,
                                uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);

#endif
