#include "crypto/ecdsa_p256.h"

#include "crypto/crypto.h"

static crypto_ecdsa_p256_verifier_fn s_verifier;
static void *s_verifier_context;

void Crypto_EcdsaP256_SetVerifier(crypto_ecdsa_p256_verifier_fn verifier, void *context)
{
    s_verifier         = verifier;
    s_verifier_context = context;
}

static int Base64Value(char character)
{
    if ((character >= 'A') && (character <= 'Z'))
        return character - 'A';
    if ((character >= 'a') && (character <= 'z'))
        return character - 'a' + 26;
    if ((character >= '0') && (character <= '9'))
        return character - '0' + 52;
    if (character == '+')
        return 62;
    if (character == '/')
        return 63;
    return -1;
}

static int IsStrictBase64(const char *data, size_t size)
{
    size_t padding = 0U;
    size_t content_size;
    size_t i;

    if ((data == NULL) || (size == 0U) || (size > CRYPTO_ECDSA_P256_BASE64_MAX_SIZE) ||
        ((size & 3U) != 0U))
        return 0;
    if (data[size - 1U] == '=')
        ++padding;
    if ((size >= 2U) && (data[size - 2U] == '='))
        ++padding;
    content_size = size - padding;
    for (i = 0U; i < content_size; ++i)
        if (Base64Value(data[i]) < 0)
            return 0;
    for (; i < size; ++i)
        if (data[i] != '=')
            return 0;

    /* RFC 4648 canonical encoding requires unused bits to be zero. */
    if ((padding == 1U) && ((Base64Value(data[size - 2U]) & 0x03) != 0))
        return 0;
    if ((padding == 2U) && ((Base64Value(data[size - 3U]) & 0x0F) != 0))
        return 0;
    return 1;
}

static int ReadStrictInteger(const uint8_t *signature, size_t size, size_t *position)
{
    size_t length;
    const uint8_t *integer;

    if ((*position + 2U > size) || (signature[*position] != 0x02U))
        return 0;
    length = signature[*position + 1U];
    *position += 2U;
    if ((length == 0U) || (length > 33U) || (length > size - *position))
        return 0;
    integer = &signature[*position];
    if ((integer[0] & 0x80U) != 0U)
        return 0; /* DER INTEGER must not be negative. */
    if ((length == 33U) && ((integer[0] != 0U) || ((integer[1] & 0x80U) == 0U)))
        return 0; /* A P-256 integer uses at most 32 value bytes. */
    if ((length > 1U) && (integer[0] == 0U) && ((integer[1] & 0x80U) == 0U))
        return 0; /* Reject redundant sign padding. */
    *position += length;
    return 1;
}

static int IsStrictDerSignature(const uint8_t *signature, size_t size)
{
    size_t position = 2U;

    if ((signature == NULL) || (size < 8U) || (size > CRYPTO_ECDSA_P256_DER_MAX_SIZE) ||
        (signature[0] != 0x30U) || (signature[1] >= 0x80U) || ((size_t) signature[1] != size - 2U))
        return 0;
    return ReadStrictInteger(signature, size, &position) &&
           ReadStrictInteger(signature, size, &position) && position == size;
}

firmware_status_t
Crypto_EcdsaP256VerifyDer(const uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE],
                          const uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE], const uint8_t *signature,
                          size_t signature_size)
{
    if ((public_key == NULL) || (digest == NULL) || (signature == NULL) ||
        (public_key[0] != 0x04U) || !IsStrictDerSignature(signature, signature_size))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    /* Signature verification is deliberately injected at the product layer.
     * This utility validates wire encoding but does not embed a heavyweight
     * trust store or crypto backend. */
    return s_verifier == NULL
               ? FIRMWARE_STATUS_NOT_SUPPORTED
               : s_verifier(public_key, digest, signature, signature_size, s_verifier_context);
}

firmware_status_t
Crypto_EcdsaP256VerifyBase64Der(const uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE],
                                const uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE],
                                const char *signature, size_t signature_size)
{
    uint8_t der[CRYPTO_ECDSA_P256_DER_MAX_SIZE];
    size_t der_size = 0U;
    firmware_status_t status;

    if (!IsStrictBase64(signature, signature_size))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    {
        size_t i, out = 0U;
        for (i = 0U; i < signature_size && signature[i] != '='; i += 4U)
        {
            int a = Base64Value(signature[i]), b = Base64Value(signature[i + 1U]);
            int c = signature[i + 2U] == '=' ? 0 : Base64Value(signature[i + 2U]);
            int d = signature[i + 3U] == '=' ? 0 : Base64Value(signature[i + 3U]);
            if (a < 0 || b < 0 || c < 0 || d < 0 || out + 3U > sizeof(der))
            {
                Crypto_SecureZero(der, sizeof(der));
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            }
            der[out++] = (uint8_t) ((a << 2) | (b >> 4));
            if (signature[i + 2U] != '=')
                der[out++] = (uint8_t) ((b << 4) | (c >> 2));
            if (signature[i + 3U] != '=')
                der[out++] = (uint8_t) ((c << 6) | d);
        }
        der_size = out;
    }
    if (der_size == 0U)
    {
        Crypto_SecureZero(der, sizeof(der));
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = Crypto_EcdsaP256VerifyDer(public_key, digest, der, der_size);
    Crypto_SecureZero(der, sizeof(der));
    return status;
}
