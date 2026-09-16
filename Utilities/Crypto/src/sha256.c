#include "crypto/sha256.h"

#include <string.h>

#include "crypto/crypto.h"
#include "mbedtls/sha256.h"

#define CRYPTO_SHA256_STATE_ACTIVE 0x53484132UL

_Static_assert(sizeof(mbedtls_sha256_context) <= CRYPTO_SHA256_CONTEXT_STORAGE_SIZE,
               "CRYPTO_SHA256_CONTEXT_STORAGE_SIZE is too small");

static mbedtls_sha256_context *Native(crypto_sha256_context_t *context)
{
    return (mbedtls_sha256_context *) (void *) context->native.bytes;
}

firmware_status_t Crypto_Sha256Init(crypto_sha256_context_t *context)
{
    mbedtls_sha256_context *native;

    if (context == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(context, 0, sizeof(*context));
    native = Native(context);
    mbedtls_sha256_init(native);
    if (mbedtls_sha256_starts_ret(native, 0) != 0)
    {
        mbedtls_sha256_free(native);
        Crypto_SecureZero(context, sizeof(*context));
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    context->state = CRYPTO_SHA256_STATE_ACTIVE;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Crypto_Sha256Update(crypto_sha256_context_t *context, const uint8_t *data,
                                      size_t size)
{
    if ((context == NULL) || (context->state != CRYPTO_SHA256_STATE_ACTIVE) ||
        ((data == NULL) && (size != 0U)))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (size == 0U)
        return FIRMWARE_STATUS_OK;
    return mbedtls_sha256_update_ret(Native(context), data, size) == 0
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t Crypto_Sha256Finish(crypto_sha256_context_t *context,
                                      uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE])
{
    int result;

    if ((context == NULL) || (digest == NULL) || (context->state != CRYPTO_SHA256_STATE_ACTIVE))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result = mbedtls_sha256_finish_ret(Native(context), digest);
    mbedtls_sha256_free(Native(context));
    Crypto_SecureZero(context, sizeof(*context));
    return result == 0 ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_INVALID_STATE;
}

void Crypto_Sha256Abort(crypto_sha256_context_t *context)
{
    if (context == NULL)
        return;
    if (context->state == CRYPTO_SHA256_STATE_ACTIVE)
        mbedtls_sha256_free(Native(context));
    Crypto_SecureZero(context, sizeof(*context));
}

firmware_status_t Crypto_Sha256(const uint8_t *data, size_t size,
                                uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE])
{
    crypto_sha256_context_t context;
    firmware_status_t status;

    if (((data == NULL) && (size != 0U)) || (digest == NULL))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = Crypto_Sha256Init(&context);
    if (FirmwareStatus_IsOk(status))
        status = Crypto_Sha256Update(&context, data, size);
    if (FirmwareStatus_IsOk(status))
        return Crypto_Sha256Finish(&context, digest);
    Crypto_Sha256Abort(&context);
    return status;
}
