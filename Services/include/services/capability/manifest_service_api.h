/**
 * @file manifest_service_api.h
 * @brief Strict Manifest parsing and integrity hashing API.
 */
#ifndef SERVICES_MANIFEST_SERVICE_API_H
#define SERVICES_MANIFEST_SERVICE_API_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/manifest_types.h"

#define MANIFEST_SERVICE_MAX_DOCUMENT_SIZE 16384U

struct manifest_service;

/** Parse, validate, and hash one complete production Manifest. */
firmware_status_t ManifestService_ParseAndValidate(
    struct manifest_service *service,
    const uint8_t *data,
    uint32_t size,
    validated_manifest_t *manifest);

#endif
