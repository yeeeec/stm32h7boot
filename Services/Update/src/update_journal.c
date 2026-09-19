#include "update/update_journal.h"

#include <stddef.h>

#include "platform/platform_nv_storage.h"

#define UPDATE_JOURNAL_STORAGE_OFFSET 64U

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

firmware_status_t UpdateJournal_Read(update_journal_record_t *record)
{
    firmware_status_t status;
    if (record == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = PlatformNvStorage_Read(UPDATE_JOURNAL_STORAGE_OFFSET, record,
                                    sizeof(*record));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    if ((record->magic != UPDATE_JOURNAL_MAGIC) ||
        (record->format_version != UPDATE_JOURNAL_FORMAT_VERSION) ||
        (record->state < UPDATE_JOURNAL_IDLE) ||
        (record->state > UPDATE_JOURNAL_COMMITTING) ||
        (record->crc32 != Crc32(record, offsetof(update_journal_record_t, crc32))))
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateJournal_Write(const update_journal_record_t *record)
{
    update_journal_record_t stored;
    if ((record == NULL) || (record->state < UPDATE_JOURNAL_IDLE) ||
        (record->state > UPDATE_JOURNAL_COMMITTING))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    stored = *record;
    stored.magic = UPDATE_JOURNAL_MAGIC;
    stored.format_version = UPDATE_JOURNAL_FORMAT_VERSION;
    stored.crc32 = Crc32(&stored, offsetof(update_journal_record_t, crc32));
    return PlatformNvStorage_Write(UPDATE_JOURNAL_STORAGE_OFFSET, &stored,
                                   sizeof(stored));
}

firmware_status_t UpdateJournal_Clear(void)
{
    const update_journal_record_t empty = {0};
    return PlatformNvStorage_Write(UPDATE_JOURNAL_STORAGE_OFFSET, &empty,
                                   sizeof(empty));
}
