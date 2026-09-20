#include "boot/boot_mailbox.h"

#include <stddef.h>

#include "platform/platform_retained_memory.h"
#include "platform/platform_system.h"

_Static_assert(sizeof(BootRequestMessage_t) <= PLATFORM_RETAINED_MEMORY_SIZE,
               "boot request must fit retained memory");
_Static_assert(sizeof(BootRequestMessage_t) == 52U,
               "boot request format must remain fixed");

static uint32_t mailbox_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    for (index = 0U; index < size; ++index)
    {
        uint32_t bit;
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int mailbox_is_valid(const BootRequestMessage_t *request)
{
    return request->magic == BOOT_REQUEST_MAGIC &&
           request->format_version == BOOT_REQUEST_FORMAT_VERSION &&
           request->request == BOOT_REQUEST_UPDATE &&
           request->crc32 == mailbox_crc32(request, offsetof(BootRequestMessage_t, crc32));
}

firmware_status_t UpdateBootMailbox_Write(const BootRequestMessage_t *request)
{
    BootRequestMessage_t stored;

    if (request == NULL || request->request != BOOT_REQUEST_UPDATE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    stored = *request;
    stored.magic = BOOT_REQUEST_MAGIC;
    stored.format_version = BOOT_REQUEST_FORMAT_VERSION;
    stored.crc32 = mailbox_crc32(&stored, offsetof(BootRequestMessage_t, crc32));
    return PlatformRetainedMemory_Write(&stored, sizeof(stored));
}

firmware_status_t UpdateBootMailbox_Take(BootRequestMessage_t *request)
{
    BootRequestMessage_t stored;
    uint32_t reset_cause;
    firmware_status_t status;

    if (request == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    status = PlatformRetainedMemory_Read(&stored, sizeof(stored));
    reset_cause = PlatformSystem_GetResetCause();
    PlatformSystem_ClearResetCause();
    UpdateBootMailbox_Clear();
    if (FirmwareStatus_IsError(status))
        return status;
    if ((reset_cause & PLATFORM_RESET_CAUSE_SOFTWARE) == 0U || !mailbox_is_valid(&stored))
        return FIRMWARE_STATUS_NOT_FOUND;

    *request = stored;
    return FIRMWARE_STATUS_OK;
}

void UpdateBootMailbox_Clear(void)
{
    PlatformRetainedMemory_Clear();
}
