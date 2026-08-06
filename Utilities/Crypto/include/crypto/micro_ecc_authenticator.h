/**
 * @file micro_ecc_authenticator.h
 * @brief secp256r1 SHA-256/ECDSA image authenticator provider.
 */
#ifndef CRYPTO_MICRO_ECC_AUTHENTICATOR_H
#define CRYPTO_MICRO_ECC_AUTHENTICATOR_H

#include <stdint.h>

#include "firmware/image_authenticator.h"
#include "crypto/sha256.h"

#define MICRO_ECC_P256_PUBLIC_KEY_SIZE 64U
#define MICRO_ECC_P256_SIGNATURE_SIZE   64U
#define MICRO_ECC_KEY_ID_MAX_SIZE       31U

/** Provider configuration copied during initialization. */
typedef struct
{
    const char *key_id; /**< ASCII key identifier copied into the provider. */
    const uint8_t *public_key; /**< 64-byte uncompressed X||Y P-256 key. */
} micro_ecc_authenticator_config_t;

/** Reusable authenticator with one mutable SHA-256 context. */
typedef struct
{
    image_authenticator_t interface;
    uint8_t public_key[MICRO_ECC_P256_PUBLIC_KEY_SIZE];
    char key_id[MICRO_ECC_KEY_ID_MAX_SIZE + 1U];
    sha256_context_t hash;
    int initialized;
} micro_ecc_authenticator_t;

/** Initialize a provider with one copied P-256 public key and Key ID. */
firmware_status_t MicroEccAuthenticator_Init(
    micro_ecc_authenticator_t *authenticator,
    const micro_ecc_authenticator_config_t *config);

/** Return the provider interface owned by an authenticator. */
const image_authenticator_t *MicroEccAuthenticator_Interface(
    const micro_ecc_authenticator_t *authenticator);

#endif
