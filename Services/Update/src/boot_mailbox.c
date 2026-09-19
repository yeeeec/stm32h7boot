#include "update/boot_mailbox.h"

#include <stddef.h>

#include "platform/platform_retained_memory.h"
#include "platform/platform_system.h"

_Static_assert(sizeof(BootRequestMessage_t) <= PLATFORM_RETAINED_MEMORY_SIZE,
               "boot request must fit in retained memory");

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

static int IsValid(const BootRequestMessage_t *message)
{
    return (message->magic == BOOT_REQUEST_MAGIC) &&
           (message->format_version == BOOT_REQUEST_FORMAT_VERSION) &&
           (message->crc32 == Crc32(message, offsetof(BootRequestMessage_t, crc32)));
}

firmware_status_t UpdateBootMailbox_Write(const BootRequestMessage_t *message)
{
    BootRequestMessage_t stored;
    if (message == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    stored = *message;
    stored.magic = BOOT_REQUEST_MAGIC;
    stored.format_version = BOOT_REQUEST_FORMAT_VERSION;
    stored.crc32 = Crc32(&stored, offsetof(BootRequestMessage_t, crc32));
    return PlatformRetainedMemory_Write(&stored, sizeof(stored));
}

firmware_status_t UpdateBootMailbox_Take(BootRequestMessage_t *message)
{
    BootRequestMessage_t stored;
    uint32_t reset_cause;
    firmware_status_t status;

    if (message == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = PlatformRetainedMemory_Read(&stored, sizeof(stored));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    reset_cause = PlatformSystem_GetResetCause();
    PlatformSystem_ClearResetCause();
    if ((reset_cause != PLATFORM_RESET_CAUSE_SOFTWARE) || !IsValid(&stored))
    {
        UpdateBootMailbox_Clear();
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    *message = stored;
    UpdateBootMailbox_Clear();
    return FIRMWARE_STATUS_OK;
}

void UpdateBootMailbox_Clear(void)
{
    PlatformRetainedMemory_Clear();
}
