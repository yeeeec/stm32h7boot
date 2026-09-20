#ifndef FIRMWARE_BOOT_REQUEST_H
#define FIRMWARE_BOOT_REQUEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BOOT_REQUEST_MAGIC          0x504D424CUL
#define BOOT_REQUEST_FORMAT_VERSION 1U
#define BOOT_REQUEST_NONE           0U
#define BOOT_REQUEST_UPDATE         0x55504454UL

    typedef struct
    {
        uint32_t magic;
        uint32_t format_version;
        uint32_t request;
        uint32_t firmware_version;
        uint8_t manifest_sha256[32];
        uint32_t crc32;
    } BootRequestMessage_t;

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_BOOT_REQUEST_H */
