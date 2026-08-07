/**
 * @file update_request_types.h
 * @brief Trusted update request domain model.
 */
#ifndef SERVICES_UPDATE_REQUEST_TYPES_H
#define SERVICES_UPDATE_REQUEST_TYPES_H

#include <stdint.h>

#define UPDATE_REQUEST_FORMAT_VERSION      1U
#define UPDATE_REQUEST_PACKAGE_ID_MAX_SIZE 63U
#define UPDATE_REQUEST_MANIFEST_HASH_SIZE  32U

typedef struct
{
    uint32_t format_version;
    uint8_t requested;
    char package_id[UPDATE_REQUEST_PACKAGE_ID_MAX_SIZE + 1U];
    uint8_t manifest_sha256[UPDATE_REQUEST_MANIFEST_HASH_SIZE];
} update_request_t;

#endif
