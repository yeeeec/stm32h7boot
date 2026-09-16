#ifndef PLATFORM_BOOT_CONTROL_H
#define PLATFORM_BOOT_CONTROL_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_BOOT_CONTROL_MAGIC          0x4254434CUL
#define PLATFORM_BOOT_CONTROL_FORMAT_VERSION 1UL

typedef enum
{
    PLATFORM_BOOT_REQUEST_NONE = 0,
    PLATFORM_BOOT_REQUEST_UPDATE = 1
} platform_boot_request_t;

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t request;
    uint8_t manifest_sha256[32];
    uint32_t crc32;
} platform_boot_control_t;

firmware_status_t PlatformBootControl_Init(void);

firmware_status_t PlatformBootControl_Read(
    platform_boot_control_t *control);

firmware_status_t PlatformBootControl_Write(
    const platform_boot_control_t *control);

firmware_status_t PlatformBootControl_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_BOOT_CONTROL_H */
