#ifndef FIRMWARE_UPDATE_MANIFEST_H
#define FIRMWARE_UPDATE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Strict, bounded manifest-v1 parser. It performs no storage or heap access. */
    firmware_status_t UpdateManifest_Parse(const uint8_t *json, size_t length,
                                           update_manifest_t *manifest);

    /* Emit the canonical signing payload with signing.signature excluded. */
    firmware_status_t UpdateManifest_Canonicalize(const update_manifest_t *manifest, char *buffer,
                                                  size_t capacity, size_t *length);

    /* SHA-256 of the canonical signing payload, encoded as lower-case hexadecimal. */
    firmware_status_t UpdateManifest_Digest(const update_manifest_t *manifest, uint8_t digest[32]);

    firmware_status_t UpdateManifest_Hash(const update_manifest_t *manifest,
                                          char output[UPDATE_SHA256_HEX_LENGTH + 1U]);

    int UpdateVersion_Compare(const update_version_t *left, const update_version_t *right);
    int UpdatePackage_IsValidId(const char *package_id);
    int UpdatePackage_IsValidComponentFileName(const char *file_name);

    /* Trust-store selection remains an Update policy decision. */
    typedef firmware_status_t (*update_manifest_signature_verifier_t)(
        const char *key_id, const uint8_t digest[32], const char *signature,
        const char *signature_encoding, void *context);

    void UpdateManifest_SetSignatureVerifier(update_manifest_signature_verifier_t verifier,
                                             void *context);
    firmware_status_t UpdateManifest_VerifySignature(const update_manifest_t *manifest,
                                                     const uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif
