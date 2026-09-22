#include "crypto/crypto.h"

#include <stdint.h>

int Crypto_ConstantTimeEqual(const void *left, const void *right, size_t size)
{
    const uint8_t *a   = (const uint8_t *) left;
    const uint8_t *b   = (const uint8_t *) right;
    uint8_t difference = 0U;
    size_t i;

    if (((left == NULL) || (right == NULL)) && (size != 0U))
        return 0;
    for (i = 0U; i < size; ++i)
        difference |= (uint8_t) (a[i] ^ b[i]);
    return difference == 0U;
}

void Crypto_SecureZero(void *data, size_t size)
{
    volatile uint8_t *cursor = (volatile uint8_t *) data;

    if (data == NULL)
        return;
    while (size-- != 0U)
        *cursor++ = 0U;
}
