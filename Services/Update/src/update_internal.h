#ifndef UPDATE_INTERNAL_H
#define UPDATE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "firmware/update_journal.h"
#include "update/component_registry.h"
#include "update/update_types.h"
#include "update/update_request.h"

#define UPDATE_MANIFEST_MAX_SIZE       8192U
#define UPDATE_MANIFEST_MAX_COMPONENTS 16U
#define UPDATE_COMPONENT_NAME_MAX      16U
#define UPDATE_COMPONENT_FILE_MAX      96U
#define UPDATE_COMPONENT_FORMAT_MAX    24U
#define UPDATE_PACKAGE_ID_MAX          64U
#define UPDATE_SHA256_HEX_LENGTH       64U
#define UPDATE_KEY_ID_MAX              64U
#define UPDATE_SIGNATURE_MAX           192U

typedef struct
{
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    uint32_t build;
} update_version_t;

typedef struct
{
    char name[UPDATE_COMPONENT_NAME_MAX];
    char file[UPDATE_COMPONENT_FILE_MAX];
    char format[UPDATE_COMPONENT_FORMAT_MAX];
    char sha256[UPDATE_SHA256_HEX_LENGTH + 1U];
    uint32_t size;
    image_target_t target;
    uint32_t mask_bit;
    uint8_t installation_order;
    uint8_t has_crc32;
    uint32_t crc32;
} update_manifest_component_t;

typedef struct
{
    uint32_t format_version;
    char package_id[UPDATE_PACKAGE_ID_MAX];
    update_version_t release;
    char product[32];
    char hardware[48];
    update_version_t minimum_bootloader_version;
    uint8_t minimum_bootloader_version_is_string;
    char algorithm[32];
    char key_id[UPDATE_KEY_ID_MAX];
    char payload_format[64];
    char signature_encoding[24];
    char signature[UPDATE_SIGNATURE_MAX];
    char created_at[32];
    uint8_t has_signing;
    update_manifest_component_t components[UPDATE_MANIFEST_MAX_COMPONENTS];
    uint32_t component_count;
    uint32_t component_mask;
} update_manifest_t;

typedef struct
{
    update_manifest_t manifest;
    uint8_t manifest_sha256[32];
    uint8_t raw_manifest_sha256[32];
    uint8_t canonical_signing_sha256[32];
    char root[192];
} update_package_t;

typedef struct
{
    update_failure_t failure;
    firmware_status_t status;
} update_operation_result_t;

firmware_status_t UpdateManifest_Parse(const uint8_t *json, size_t length,
                                       update_manifest_t *manifest);
firmware_status_t UpdateManifest_ValidateTarget(const update_manifest_t *manifest);
firmware_status_t UpdateManifest_Canonicalize(const update_manifest_t *manifest, char *buffer,
                                              size_t capacity, size_t *length);
firmware_status_t UpdateManifest_Digest(const update_manifest_t *manifest, uint8_t digest[32]);
firmware_status_t UpdateManifest_Hash(const update_manifest_t *manifest,
                                      char output[UPDATE_SHA256_HEX_LENGTH + 1U]);
int UpdateVersion_Compare(const update_version_t *left, const update_version_t *right);
int UpdatePackage_IsValidId(const char *package_id);
int UpdatePackage_IsValidComponentFileName(const char *file_name);

update_operation_result_t PackageReader_Validate(const char *root,
                                                 const uint8_t *expected_manifest_digest,
                                                 int verify_payload_hashes,
                                                 update_package_t *package);
update_operation_result_t PackageReader_ValidateRequest(const char *root,
                                                        const update_request_t *request,
                                                        const uint8_t *expected_manifest_digest,
                                                        int verify_payload_hashes,
                                                        update_package_t *package);
firmware_status_t PackageReader_ValidateUpdateRoot(void);

const update_component_descriptor_t *UpdateComponent_Find(const char *name);
const update_component_descriptor_t *UpdateComponent_FindByTarget(image_target_t target);
uint32_t UpdateComponent_DeriveMask(const update_manifest_t *manifest);
firmware_status_t UpdateComponent_ValidateRanges(const update_manifest_t *manifest);

update_operation_result_t ImageInstaller_Install(const char *root,
                                                 const update_manifest_component_t *component);

firmware_status_t CurrentStore_Verify(void);
firmware_status_t CurrentStore_Read(update_package_t *package);
firmware_status_t CurrentStore_Commit(const update_package_t *package,
                                      const uint8_t expected_manifest_sha256[32]);
firmware_status_t CurrentStore_CleanupUpdate(void);
firmware_status_t RuntimeVerifier_Validate(void);

firmware_status_t UpdateJournal_Read(update_journal_record_t *record);
firmware_status_t UpdateJournal_Write(const update_journal_record_t *record);

typedef enum
{
    UPDATE_VERSION_REJECT = 0,
    UPDATE_VERSION_ALLOW  = 1
} update_version_decision_t;

update_version_decision_t VersionPolicy_Check(const update_version_t *update,
                                              const update_version_t *current, int current_exists);
firmware_status_t VersionPolicy_ValidateMinimumBootloader(const update_version_t *required,
                                                          const update_version_t *running);

int UpdateHex_DecodeSha256(const char *text, uint8_t digest[32]);

#endif /* UPDATE_INTERNAL_H */
