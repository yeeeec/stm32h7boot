#ifndef FIRMWARE_STORAGE_TYPES_H
#define FIRMWARE_STORAGE_TYPES_H

#include <stdint.h>

#include "firmware/status.h"

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
#define STORAGE_LOG_PATH             "/LOG/system.log"
#define STORAGE_UPDATE_ROOT          "/UPDATE"
#define STORAGE_USB_FIRMWARE_ROOT    "/firmware"
#define STORAGE_SD_FIRMWARE_ROOT     "/UPDATE/firmware"
#define STORAGE_BOOT_REQUEST_PATH    "/UPDATE/boot_update_request.json"

typedef enum
{
    STORAGE_OPEN_READ = 0,
    STORAGE_OPEN_WRITE_TRUNCATE,
    STORAGE_OPEN_WRITE_APPEND,
    STORAGE_OPEN_READ_WRITE
} storage_open_mode_t;

typedef struct
{
    uint32_t handle;
    uint32_t size;
    uint32_t crc32;
} storage_file_info_t;

typedef struct
{
    char name[STORAGE_PATH_MAX];
    uint32_t size;
    uint8_t is_directory;
    uint8_t end_of_directory;
} storage_dir_entry_t;

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
