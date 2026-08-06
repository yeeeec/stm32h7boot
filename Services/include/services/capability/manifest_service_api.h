/**
 * @file manifest_service_api.h
 * @brief Strict signed Manifest parsing and authentication API.
 */
#ifndef SERVICES_MANIFEST_SERVICE_API_H
#define SERVICES_MANIFEST_SERVICE_API_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/manifest_types.h"

#define MANIFEST_SERVICE_MAX_DOCUMENT_SIZE 16384U

struct manifest_service;

/** Parse, validate, hash, and authenticate one complete production Manifest. */
firmware_status_t ManifestService_ParseAndVerify(
    struct manifest_service *service,
    const uint8_t *data,
    uint32_t size,
    validated_manifest_t *manifest);

#endif
