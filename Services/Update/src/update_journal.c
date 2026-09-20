#include "update_internal.h"

#include <stddef.h>
#include <string.h>

#include "checksum/crc32.h"
#include "platform/platform_nv_storage.h"
#include "update_config.h"

#define UPDATE_JOURNAL_SLOT_COUNT 2U

static uint32_t journal_crc(const update_journal_record_t *record)
{
    return Checksum_Crc32Ieee((const uint8_t *) record, offsetof(update_journal_record_t, crc32));
}

static int journal_valid(const update_journal_record_t *record)
{
    return record->magic == UPDATE_JOURNAL_MAGIC &&
           record->format_version == UPDATE_JOURNAL_FORMAT_VERSION &&
           record->state <= UPDATE_STATE_FAILED && record->source <= UPDATE_SOURCE_ROLLBACK &&
           (record->flags & ~UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING) == 0U &&
           record->crc32 == journal_crc(record);
}

static int journal_empty(const update_journal_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *) record;
    size_t index;
    int zero   = 1;
    int erased = 1;

    for (index = 0U; index < sizeof(*record); ++index)
    {
        zero &= bytes[index] == 0U;
        erased &= bytes[index] == 0xFFU;
    }
    return zero || erased;
}

static firmware_status_t read_slot(uint32_t slot, update_journal_record_t *record)
{
    firmware_status_t status = PlatformNvStorage_Read(
        UPDATE_JOURNAL_STORAGE_OFFSET + slot * UPDATE_JOURNAL_SLOT_SIZE, record, sizeof(*record));

    if (FirmwareStatus_IsError(status))
        return status;
    if (journal_valid(record))
        return FIRMWARE_STATUS_OK;
    return journal_empty(record) ? FIRMWARE_STATUS_NOT_FOUND
                                 : FIRMWARE_STATUS_AUTHENTICATION_FAILED;
}

static firmware_status_t read_latest(update_journal_record_t *record, uint32_t *active_slot)
{
    update_journal_record_t slots[UPDATE_JOURNAL_SLOT_COUNT];
    firmware_status_t statuses[UPDATE_JOURNAL_SLOT_COUNT];
    int selected = -1;
    uint32_t slot;

    for (slot = 0U; slot < UPDATE_JOURNAL_SLOT_COUNT; ++slot)
    {
        statuses[slot] = read_slot(slot, &slots[slot]);
        if (statuses[slot] == FIRMWARE_STATUS_OK &&
            (selected < 0 ||
             (int32_t) (slots[slot].sequence - slots[(uint32_t) selected].sequence) > 0))
            selected = (int) slot;
    }
    if (selected >= 0)
    {
        *record = slots[(uint32_t) selected];
        if (active_slot != NULL)
            *active_slot = (uint32_t) selected;
        return FIRMWARE_STATUS_OK;
    }
    if (statuses[0] != FIRMWARE_STATUS_NOT_FOUND)
        return statuses[0];
    if (statuses[1] != FIRMWARE_STATUS_NOT_FOUND)
        return statuses[1];
    return FIRMWARE_STATUS_NOT_FOUND;
}

firmware_status_t UpdateJournal_Read(update_journal_record_t *record)
{
    if (record == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return read_latest(record, NULL);
}

firmware_status_t UpdateJournal_Write(const update_journal_record_t *record)
{
    update_journal_record_t latest;
    update_journal_record_t stored;
    update_journal_record_t verified;
    uint32_t active_slot = 1U;
    firmware_status_t status;

    if (record == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    stored = *record;
    status = read_latest(&latest, &active_slot);
    if (status != FIRMWARE_STATUS_OK && status != FIRMWARE_STATUS_NOT_FOUND)
        return status;
    stored.magic          = UPDATE_JOURNAL_MAGIC;
    stored.format_version = UPDATE_JOURNAL_FORMAT_VERSION;
    stored.sequence       = status == FIRMWARE_STATUS_OK ? latest.sequence + 1U : 1U;
    stored.crc32          = journal_crc(&stored);
    status = PlatformNvStorage_Write(UPDATE_JOURNAL_STORAGE_OFFSET +
                                         (active_slot ^ 1U) * UPDATE_JOURNAL_SLOT_SIZE,
                                     &stored, sizeof(stored));
    if (FirmwareStatus_IsError(status))
        return status;
    status = read_slot(active_slot ^ 1U, &verified);
    if (FirmwareStatus_IsError(status) || memcmp(&stored, &verified, sizeof(stored)) != 0)
        return FIRMWARE_STATUS_IO_ERROR;
    return FIRMWARE_STATUS_OK;
}
