#ifndef PLATFORM_BOOT_MAILBOX_H
#define PLATFORM_BOOT_MAILBOX_H

#include <stdint.h>

#include "firmware/status.h"

typedef struct
{
    uint32_t request;
    uint32_t version;
    uint8_t manifest_sha256[32];
    uint32_t crc;
} platform_boot_request_t;

firmware_status_t PlatformBootMailbox_Write(const platform_boot_request_t *request);
firmware_status_t PlatformBootMailbox_Take(platform_boot_request_t *request);
void PlatformBootMailbox_Clear(void);

#endif /* PLATFORM_BOOT_MAILBOX_H */
