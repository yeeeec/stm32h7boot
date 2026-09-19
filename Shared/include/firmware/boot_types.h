#ifndef FIRMWARE_BOOT_TYPES_H
#define FIRMWARE_BOOT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BOOT_MAGIC          0x4254434CUL /* "BTCL" */
#define BOOT_REQUEST_NONE   0x00000000UL
#define BOOT_REQUEST_UPDATE 0x55504454UL /* "UPDT" */

    typedef struct
    {
        uint32_t magic;
        uint32_t format_version;
        uint32_t request;
        uint8_t manifest_sha256[32];
        uint32_t check;
    } BootControl_t;

    typedef enum
    {
        BOOT_OK = 0,
        BOOT_ERR_REQUEST,
        BOOT_ERR_MANIFEST,
        BOOT_ERR_VERSION,
        BOOT_ERR_FILE,
        BOOT_ERR_HASH,
        BOOT_ERR_ERASE,
        BOOT_ERR_WRITE,
        BOOT_ERR_READBACK,
        BOOT_ERR_RECOVERY,
        BOOT_ERR_LAUNCH,
        BOOT_ERR_STORAGE,
        BOOT_ERR_PROTOCOL,
        BOOT_ERR_FATAL
    } BootError_t;

#ifdef __cplusplus
}
#endif

#endif
