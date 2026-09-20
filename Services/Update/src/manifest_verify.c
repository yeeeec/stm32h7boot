#include "update_internal.h"

#include <string.h>

#include "crypto/ecdsa_p256.h"
#include "update_config.h"

typedef struct
{
    const char *key_id;
    uint8_t public_key[CRYPTO_ECDSA_P256_PUBLIC_KEY_SIZE];
} trusted_key_t;

/* This default anchor intentionally fails closed until the product key is
 * provisioned.  Trust selection remains static and is never callback-driven. */
static const trusted_key_t s_trust_store[] = {
    {UPDATE_TRUSTED_KEY_ID, UPDATE_TRUSTED_PUBLIC_KEY_BYTES}};

static const trusted_key_t *find_key(const char *key_id)
{
    size_t index;

    for (index = 0U; index < sizeof(s_trust_store) / sizeof(s_trust_store[0]); ++index)
        if (strcmp(key_id, s_trust_store[index].key_id) == 0)
            return &s_trust_store[index];
    return NULL;
}

static int key_is_provisioned(const trusted_key_t *key)
{
    size_t index;
    uint8_t nonzero = 0U;

    for (index = 1U; index < sizeof(key->public_key); ++index)
        nonzero |= key->public_key[index];
    return key->public_key[0] == 0x04U && nonzero != 0U;
}

firmware_status_t ManifestVerify_Verify(const update_manifest_t *manifest,
                                        const uint8_t digest[32])
{
    const trusted_key_t *key;

    if (manifest == NULL || digest == NULL || manifest->has_signing == 0U ||
        strcmp(manifest->algorithm, "ECDSA-P256-SHA256") != 0 ||
        strcmp(manifest->payload_format, "canonical-json-v1") != 0 ||
        strcmp(manifest->signature_encoding, "base64-der") != 0)
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;

    key = find_key(manifest->key_id);
    if (key == NULL || !key_is_provisioned(key))
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    return Crypto_EcdsaP256VerifyBase64Der(key->public_key, digest,
                                           manifest->signature,
                                           strlen(manifest->signature)) == 0
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_AUTHENTICATION_FAILED;
}
