#ifndef UPDATE_PACKAGE_H
#define UPDATE_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "update/component_registry.h"
#include "update_model.h"

firmware_status_t PackageReader_Validate(const char *root, update_package_t *package);
firmware_status_t UpdateManifest_Parse(const uint8_t *json, size_t length,
                                       update_manifest_t *manifest);
firmware_status_t UpdateManifest_ValidateTarget(const update_manifest_t *manifest);
int UpdateVersion_Compare(const update_version_t *left, const update_version_t *right);
int UpdatePackage_IsValidId(const char *package_id);
int UpdatePackage_IsValidComponentFileName(const char *file_name);
int UpdateHex_DecodeSha256(const char *text, uint8_t digest[32]);

uint32_t UpdateComponent_RequiredMask(void);
uint32_t UpdateComponent_DeriveMask(const update_manifest_t *manifest);
firmware_status_t UpdateComponent_ValidateRanges(const update_manifest_t *manifest);

#endif /* UPDATE_PACKAGE_H */
