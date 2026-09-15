#ifndef FIRMWARE_UPDATE_TYPES_H
#define FIRMWARE_UPDATE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UPDATE_MANIFEST_MAX_SIZE       8192U
#define UPDATE_MANIFEST_MAX_COMPONENTS 8U
#define UPDATE_COMPONENT_NAME_MAX      16U
#define UPDATE_COMPONENT_FILE_MAX      96U
#define UPDATE_COMPONENT_FORMAT_MAX    24U
#define UPDATE_PACKAGE_ID_MAX          64U
#define UPDATE_SHA256_HEX_LENGTH       64U
#define UPDATE_KEY_ID_MAX              64U
#define UPDATE_SIGNATURE_MAX           192U

    typedef struct
    {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
        uint32_t build;
    } update_version_t;

    typedef struct
    {
        char name[UPDATE_COMPONENT_NAME_MAX];
        char file[UPDATE_COMPONENT_FILE_MAX];
        char format[UPDATE_COMPONENT_FORMAT_MAX];
        char sha256[UPDATE_SHA256_HEX_LENGTH + 1U];
        uint32_t size;
        uint32_t mask;
        uint8_t has_crc32;
        uint32_t crc32;
    } update_manifest_component_t;

    typedef struct
    {
        uint32_t format_version;
        char package_id[UPDATE_PACKAGE_ID_MAX];
        update_version_t release;
        char product[32];
        char hardware[48];
        update_version_t minimum_bootloader_version;
        uint8_t minimum_bootloader_version_is_string;
        char algorithm[32];
        char key_id[UPDATE_KEY_ID_MAX];
        char payload_format[64];
        char signature_encoding[24];
        char signature[UPDATE_SIGNATURE_MAX];
        char created_at[32];
        update_manifest_component_t components[UPDATE_MANIFEST_MAX_COMPONENTS];
        uint32_t component_count;
    } update_manifest_t;

#ifdef __cplusplus
}
#endif

#endif
