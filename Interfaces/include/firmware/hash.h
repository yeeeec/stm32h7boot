/**
 * @file hash.h
 * @brief Incremental cryptographic hash provider contract.
 */
#ifndef FIRMWARE_HASH_H
#define FIRMWARE_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define FIRMWARE_SHA256_DIGEST_SIZE 32U

typedef firmware_status_t (*hash_provider_reset_fn)(void *context);
typedef firmware_status_t (*hash_provider_update_fn)(
    void *context,
    const void *data,
    size_t size);
typedef firmware_status_t (*hash_provider_finish_fn)(
    void *context,
    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE]);

/** One provider owns one mutable incremental hash context. */
typedef struct
{
    void *context;
    hash_provider_reset_fn reset;
    hash_provider_update_fn update;
    hash_provider_finish_fn finish;
} hash_provider_t;

#endif
