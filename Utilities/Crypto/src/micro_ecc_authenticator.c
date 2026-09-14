/**
 * @file micro_ecc_authenticator.c
 * @brief SHA-256 and fixed-format secp256r1 ECDSA verification provider.
 */
#include "crypto/micro_ecc_authenticator.h"

#include <stddef.h>
#include <string.h>

#include "crypto/sha256.h"
#include "uECC.h"

static sha256_context_t *HashContext(micro_ecc_authenticator_t *authenticator)
{
    return &authenticator->hash;
}

static firmware_status_t HashReset(void *context)
{
    micro_ecc_authenticator_t *authenticator = (micro_ecc_authenticator_t *) context;

    return (authenticator == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                                   : Sha256_Reset(HashContext(authenticator));
}

static firmware_status_t HashUpdate(void *context, const void *data, size_t size)
{
    micro_ecc_authenticator_t *authenticator = (micro_ecc_authenticator_t *) context;

    return (authenticator == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                                   : Sha256_Update(HashContext(authenticator), data, size);
}

static firmware_status_t HashFinish(void *context, uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE])
{
    micro_ecc_authenticator_t *authenticator = (micro_ecc_authenticator_t *) context;

    return (authenticator == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                                   : Sha256_Finish(HashContext(authenticator), digest);
}

static firmware_status_t VerifySignature(void *context, const char *key_id,
                                         const uint8_t digest[IMAGE_AUTHENTICATOR_SHA256_SIZE],
                                         const uint8_t *signature, size_t signature_size)
{
    const micro_ecc_authenticator_t *authenticator = (const micro_ecc_authenticator_t *) context;

    if ((authenticator == NULL) || (key_id == NULL) || (digest == NULL) || (signature == NULL) ||
        (signature_size != MICRO_ECC_P256_SIGNATURE_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(key_id, authenticator->key_id) != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return (uECC_verify(authenticator->public_key, digest, IMAGE_AUTHENTICATOR_SHA256_SIZE,
                        signature, uECC_secp256r1()) != 0)
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t MicroEccAuthenticator_Init(micro_ecc_authenticator_t *authenticator,
                                             const micro_ecc_authenticator_config_t *config)
{
    size_t key_id_length;

    if ((authenticator == NULL) || (config == NULL) || (config->key_id == NULL) ||
        (config->public_key == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (authenticator->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    key_id_length = strlen(config->key_id);
    if ((key_id_length == 0U) || (key_id_length > MICRO_ECC_KEY_ID_MAX_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (uECC_valid_public_key(config->public_key, uECC_secp256r1()) == 0)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    memcpy(authenticator->public_key, config->public_key, sizeof(authenticator->public_key));
    memcpy(authenticator->key_id, config->key_id, key_id_length + 1U);
    authenticator->interface.context          = authenticator;
    authenticator->interface.hash_reset       = HashReset;
    authenticator->interface.hash_update      = HashUpdate;
    authenticator->interface.hash_finish      = HashFinish;
    authenticator->interface.verify_signature = VerifySignature;
    authenticator->initialized                = 1;
    return HashReset(authenticator);
}

const image_authenticator_t *
MicroEccAuthenticator_Interface(const micro_ecc_authenticator_t *authenticator)
{
    return (authenticator == NULL) ? NULL : &authenticator->interface;
}
