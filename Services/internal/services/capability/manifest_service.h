/**
 * @file manifest_service.h
 * @brief Composition-visible Manifest service object and dependencies.
 */
#ifndef SERVICES_MANIFEST_SERVICE_INTERNAL_H
#define SERVICES_MANIFEST_SERVICE_INTERNAL_H

#include "firmware/hash.h"
#include "services/capability/json_document.h"
#include "services/capability/manifest_service_api.h"

#define MANIFEST_SERVICE_TOKEN_CAPACITY 192U

/* The fixed Schema has 118 tokens in the production test vector. Keep the
 * parser workspace within the 4 KiB static-memory budget with headroom for
 * malformed input before schema validation rejects it. */
_Static_assert(
    (MANIFEST_SERVICE_TOKEN_CAPACITY * sizeof(json_token_t)) <= 4096U,
    "Manifest token workspace exceeds the static-memory budget");

typedef struct
{
    const hash_provider_t *hash;
} manifest_service_dependencies_t;

typedef struct manifest_service
{
    const hash_provider_t *hash;
    json_token_t tokens[MANIFEST_SERVICE_TOKEN_CAPACITY];
    int initialized;
} manifest_service_t;

firmware_status_t ManifestService_Init(
    manifest_service_t *service,
    const manifest_service_dependencies_t *dependencies);

#endif
