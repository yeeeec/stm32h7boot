#ifndef FIRMWARE_CRYPTO_ECDSA_P256_H
#define FIRMWARE_CRYPTO_ECDSA_P256_H

#include <stddef.h>
#include <stdint.h>

#include "crypto/sha256.h"
#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE 65U
#define CRYPTO_ECDSA_P256_DER_MAX_SIZE    72U
#define CRYPTO_ECDSA_P256_BASE64_MAX_SIZE 96U

    typedef firmware_status_t (*crypto_ecdsa_p256_verifier_fn)(
        const uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE],
        const uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE], const uint8_t *signature,
        size_t signature_size, void *context);

    /* Product code may register its trust-store/crypto backend.  The wire
     * parser remains usable when no backend is linked into the bootloader. */
    void Crypto_EcdsaP256_SetVerifier(crypto_ecdsa_p256_verifier_fn verifier, void *context);

    /* public_key is the SEC1 uncompressed form: 0x04 || X[32] || Y[32]. */
    firmware_status_t
    Crypto_EcdsaP256VerifyDer(const uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE],
                              const uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE],
                              const uint8_t *signature, size_t signature_size);

    /* Strict Base64 wrapper for the DER signature used by manifest format v1. */
    firmware_status_t
    Crypto_EcdsaP256VerifyBase64Der(const uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE],
                                    const uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE],
                                    const char *signature, size_t signature_size);

#ifdef __cplusplus
}
#endif

#endif
