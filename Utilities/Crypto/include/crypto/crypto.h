#ifndef FIRMWARE_CRYPTO_H
#define FIRMWARE_CRYPTO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Compare secrets without data-dependent early exit. */
    int Crypto_ConstantTimeEqual(const void *left, const void *right, size_t size);

    /* Clear sensitive memory through volatile stores. */
    void Crypto_SecureZero(void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
