/**
 * @file boot_control_types.h
 * @brief Stable Active Record and Update Request domain models.
 */
#ifndef SERVICES_BOOT_CONTROL_TYPES_H
#define SERVICES_BOOT_CONTROL_TYPES_H

#include <stdint.h>

#include "services/common/boot_types.h"

#define BOOT_CONTROL_PACKAGE_ID_HASH_SIZE 16U
#define BOOT_CONTROL_MANIFEST_HASH_SIZE   32U

typedef struct
{
    uint32_t sequence;
    boot_pair_t active_pair;
    release_version_t release_version;
    uint32_t build_number;
    uint32_t app_size;
    uint32_t app_crc32;
    uint32_t gui_size;
    uint32_t gui_crc32;
    uint8_t package_id_hash[BOOT_CONTROL_PACKAGE_ID_HASH_SIZE];
    uint8_t manifest_sha256[BOOT_CONTROL_MANIFEST_HASH_SIZE];
} boot_active_record_t;

typedef enum
{
    BOOT_UPDATE_REASON_NONE = 0,
    BOOT_UPDATE_REASON_APPLICATION = 1,
    BOOT_UPDATE_REASON_RECOVERY = 2,
    BOOT_UPDATE_REASON_PRODUCTION = 3
} boot_update_reason_t;

typedef struct
{
    uint32_t sequence;
    int requested;
    boot_update_reason_t reason;
} boot_update_request_t;

#endif
