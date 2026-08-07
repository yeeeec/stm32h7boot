/**
 * @file update_request_service.h
 * @brief Composition-visible trusted update request parser object.
 */
#ifndef SERVICES_UPDATE_REQUEST_SERVICE_INTERNAL_H
#define SERVICES_UPDATE_REQUEST_SERVICE_INTERNAL_H

#include "firmware/hash.h"
#include "services/capability/json_document.h"
#include "services/capability/update_request_service_api.h"

#define UPDATE_REQUEST_SERVICE_MAX_DOCUMENT_SIZE 512U
#define UPDATE_REQUEST_SERVICE_TOKEN_CAPACITY     16U

typedef struct
{
    const hash_provider_t *hash;
} update_request_service_dependencies_t;

typedef struct update_request_service
{
    const hash_provider_t *hash;
    json_token_t tokens[UPDATE_REQUEST_SERVICE_TOKEN_CAPACITY];
    int initialized;
} update_request_service_t;

firmware_status_t UpdateRequestService_Init(
    update_request_service_t *service,
    const update_request_service_dependencies_t *dependencies);

#endif
