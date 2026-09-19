#include "platform/platform_boot_mailbox.h"

#include <stddef.h>

#include "platform/platform_system.h"
#include "stm32h7xx_hal.h"

#define PLATFORM_BOOT_MAILBOX_MAGIC   0x504D424CUL
#define PLATFORM_BOOT_MAILBOX_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint32_t version;
    platform_boot_request_t request;
} mailbox_storage_t;

#if defined(__GNUC__)
static mailbox_storage_t s_mailbox __attribute__((section(".noinit"), used));
#else
static mailbox_storage_t s_mailbox;
#endif

static uint32_t Crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;
    for (i = 0U; i < size; ++i)
    {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static int IsValid(const mailbox_storage_t *storage)
{
    return (storage->magic == PLATFORM_BOOT_MAILBOX_MAGIC) &&
           (storage->version == PLATFORM_BOOT_MAILBOX_VERSION) &&
           (storage->request.crc == Crc32(&storage->request,
                                          offsetof(platform_boot_request_t, crc)));
}

firmware_status_t PlatformBootMailbox_Write(const platform_boot_request_t *request)
{
    if (request == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    s_mailbox.magic = PLATFORM_BOOT_MAILBOX_MAGIC;
    s_mailbox.version = PLATFORM_BOOT_MAILBOX_VERSION;
    s_mailbox.request = *request;
    s_mailbox.request.crc = Crc32(&s_mailbox.request,
                                  offsetof(platform_boot_request_t, crc));
    __DSB();
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformBootMailbox_Take(platform_boot_request_t *request)
{
    uint32_t reset_cause;
    if (request == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    reset_cause = PlatformSystem_GetResetCause();
    PlatformSystem_ClearResetCause();
    if ((reset_cause & (PLATFORM_RESET_CAUSE_POWER_ON | PLATFORM_RESET_CAUSE_BROWN_OUT)) != 0U)
    {
        PlatformBootMailbox_Clear();
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    if (!IsValid(&s_mailbox))
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    *request = s_mailbox.request;
    PlatformBootMailbox_Clear();
    return FIRMWARE_STATUS_OK;
}

void PlatformBootMailbox_Clear(void)
{
    s_mailbox.magic = 0U;
    s_mailbox.version = 0U;
    s_mailbox.request = (platform_boot_request_t){0};
    __DSB();
}
