/**
 * @file update_request_service_api.h
 * @brief Strict trusted update request parsing and Manifest binding API.
 */
#ifndef SERVICES_UPDATE_REQUEST_SERVICE_API_H
#define SERVICES_UPDATE_REQUEST_SERVICE_API_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/manifest_types.h"
#include "services/common/update_request_types.h"

struct update_request_service;

firmware_status_t UpdateRequestService_ParseAndValidate(
    struct update_request_service *service,
    const uint8_t *data,
    uint32_t size,
    update_request_t *request);

firmware_status_t UpdateRequestService_ValidateManifestBinding(
    struct update_request_service *service,
    const update_request_t *request,
    const uint8_t *manifest_data,
    uint32_t manifest_size,
    const validated_manifest_t *manifest);

#endif
