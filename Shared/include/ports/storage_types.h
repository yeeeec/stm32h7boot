#ifndef FIRMWARE_STORAGE_TYPES_H
#define FIRMWARE_STORAGE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define STORAGE_MAX_OPEN_FILES       4U
#define STORAGE_MAX_OPEN_DIRECTORIES 2U
#define STORAGE_IO_CHUNK_SIZE        4096U
#define STORAGE_PATH_MAX             96U
#define STORAGE_PACKAGE_ID_MAX       64U
#define STORAGE_SHA256_HEX_LENGTH    64U

    typedef struct
    {
        uint32_t format_version;
        uint8_t requested;
        char package_id[STORAGE_PACKAGE_ID_MAX];
        char manifest_sha256[STORAGE_SHA256_HEX_LENGTH + 1U];
        uint32_t component_mask;
    } storage_boot_update_request_t;

#ifdef __cplusplus
}
#endif

#endif
