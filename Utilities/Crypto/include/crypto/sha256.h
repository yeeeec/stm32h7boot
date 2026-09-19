#ifndef FIRMWARE_CRYPTO_SHA256_H
#define FIRMWARE_CRYPTO_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CRYPTO_SHA256_DIGEST_SIZE          32U
#define CRYPTO_SHA256_CONTEXT_STORAGE_SIZE 128U

    /* The native mbedTLS type is hidden so callers do not depend on third-party headers. */
    typedef struct
    {
        union
        {
            uint64_t alignment;
            uint8_t bytes[CRYPTO_SHA256_CONTEXT_STORAGE_SIZE];
        } native;
        uint32_t state;
    } crypto_sha256_context_t;

    int Crypto_Sha256Init(crypto_sha256_context_t *context);
    int Crypto_Sha256Update(crypto_sha256_context_t *context, const uint8_t *data, size_t size);
    int Crypto_Sha256Finish(crypto_sha256_context_t *context,
                            uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);
    void Crypto_Sha256Abort(crypto_sha256_context_t *context);
    int Crypto_Sha256(const uint8_t *data, size_t size, uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
