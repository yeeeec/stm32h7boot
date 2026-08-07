/**
 * @file boot_control_types.h
 * @brief Stable Active Record domain model.
 */
#ifndef SERVICES_BOOT_CONTROL_TYPES_H
#define SERVICES_BOOT_CONTROL_TYPES_H

#include <stdint.h>

#include "services/common/boot_types.h"

#define BOOT_CONTROL_PACKAGE_ID_HASH_SIZE 16U
#define BOOT_CONTROL_MANIFEST_HASH_SIZE   32U
#define BOOT_CONTROL_IMAGE_HASH_SIZE      32U
#define BOOT_ACTIVE_RECORD_FORMAT_V2      2U
#define BOOT_ACTIVE_RECORD_SIZE           256U
#define BOOT_ACTIVE_RECORD_STATE_VALID    1U

typedef struct
{
    uint16_t format_version;
    uint8_t state;
    uint8_t flags;
    uint32_t sequence;
    release_version_t release_version;
    uint32_t build_number;
    uint32_t app_size;
    uint32_t gui_size;
    uint8_t package_id_hash[BOOT_CONTROL_PACKAGE_ID_HASH_SIZE];
    uint8_t manifest_sha256[BOOT_CONTROL_MANIFEST_HASH_SIZE];
    uint8_t app_sha256[BOOT_CONTROL_IMAGE_HASH_SIZE];
    uint8_t gui_sha256[BOOT_CONTROL_IMAGE_HASH_SIZE];
} boot_active_record_t;

#endif
