/**
 * @file manifest_types.h
 * @brief Strictly parsed upgrade Manifest domain types.
 */
#ifndef SERVICES_MANIFEST_TYPES_H
#define SERVICES_MANIFEST_TYPES_H

#include <stdint.h>

#include "services/common/boot_types.h"

#define MANIFEST_PACKAGE_ID_MAX_SIZE 63U
#define MANIFEST_FILE_NAME_MAX_SIZE  31U
#define MANIFEST_SHA256_SIZE         32U
#define MANIFEST_PACKAGE_HASH_SIZE   16U

typedef struct
{
    char file[MANIFEST_FILE_NAME_MAX_SIZE + 1U];
    uint32_t file_size_bytes;
    uint32_t image_size_bytes;
    uint32_t source_crc32;
    uint32_t target_crc32_app1;
    uint32_t target_crc32_app2;
    uint8_t sha256[MANIFEST_SHA256_SIZE];
    uint32_t link_address;
    uint32_t entry_offset;
    char relocation_file[MANIFEST_FILE_NAME_MAX_SIZE + 1U];
    uint32_t relocation_count;
    uint32_t relocation_crc32;
} manifest_app_component_t;

typedef struct
{
    char file[MANIFEST_FILE_NAME_MAX_SIZE + 1U];
    uint32_t file_size_bytes;
    uint32_t crc32;
    uint8_t sha256[MANIFEST_SHA256_SIZE];
} manifest_gui_component_t;

typedef struct
{
    char package_id[MANIFEST_PACKAGE_ID_MAX_SIZE + 1U];
    uint32_t build_number;
    release_version_t minimum_bootloader_version;
    release_version_t release_version;
    manifest_app_component_t app;
    manifest_gui_component_t gui;
    uint8_t manifest_sha256[MANIFEST_SHA256_SIZE];
    uint8_t package_id_hash128[MANIFEST_PACKAGE_HASH_SIZE];
} validated_manifest_t;

#endif
