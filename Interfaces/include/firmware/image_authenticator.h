/**
 * @file image_authenticator.h
 * @brief Incremental SHA-256 and signed-digest verification contract.
 */
#ifndef FIRMWARE_IMAGE_AUTHENTICATOR_H
#define FIRMWARE_IMAGE_AUTHENTICATOR_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define IMAGE_AUTHENTICATOR_SHA256_SIZE 32U

typedef firmware_status_t (*image_authenticator_hash_reset_fn)(void *context);
typedef firmware_status_t (*image_authenticator_hash_update_fn)(
    void *context,
    const void *data,
    size_t size);
typedef firmware_status_t (*image_authenticator_hash_finish_fn)(
    void *context,
    uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE]);
typedef firmware_status_t (*image_authenticator_verify_signature_fn)(
    void *context,
    const char *key_id,
    const uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE],
    const uint8_t *signature,
    size_t signature_size);

/**
 * The provider owns one mutable hash context. Signature verification must fail
 * closed for unknown key identifiers or unsupported encodings.
 */
typedef struct
{
    void *context; /**< Provider-owned hash and public-key context. */
    image_authenticator_hash_reset_fn hash_reset; /**< Begin SHA-256 hashing. */
    image_authenticator_hash_update_fn hash_update; /**< Consume hash input bytes. */
    image_authenticator_hash_finish_fn hash_finish; /**< Finalize a 32-byte digest. */
    /** Verify a provider-defined ECDSA signature encoding and fail closed. */
    image_authenticator_verify_signature_fn verify_signature;
} image_authenticator_t;

#endif
