#include "update_internal.h"

#include <stddef.h>
#include <string.h>

#include "checksum/crc32.h"
#include "platform/platform_journal_storage.h"

static uint32_t journal_crc(const update_journal_record_t *record)
{
    return Checksum_Crc32Ieee((const uint8_t *)record,
                              offsetof(update_journal_record_t, crc32));
}

static int record_erased(const update_journal_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t index;
    for (index = 0U; index < sizeof(*record); ++index)
        if (bytes[index] != 0xFFU)
            return 0;
    return 1;
}

static int state_target_valid(uint32_t state, uint32_t target)
{
    if (state == UPDATE_STATE_IDLE)
        return target == UPDATE_TARGET_NONE;
    if (state > UPDATE_STATE_JUMPING)
        return 0;
    return target == UPDATE_TARGET_UPDATE || target == UPDATE_TARGET_ROLLBACK;
}

static int record_valid(const update_journal_record_t *record)
{
    return record->magic == UPDATE_JOURNAL_MAGIC &&
           record->format_version == UPDATE_JOURNAL_FORMAT_VERSION &&
           record->record_size == sizeof(update_journal_record_t) &&
           state_target_valid(record->state, record->target) &&
           record->crc32 == journal_crc(record);
}

static firmware_status_t read_slot(uint32_t slot, update_journal_record_t *record)
{
    firmware_status_t status = PlatformJournalStorage_Read(slot, record, sizeof(*record));
    if (FirmwareStatus_IsError(status))
        return status;
    if (record_erased(record))
        return FIRMWARE_STATUS_NOT_FOUND;
    return record_valid(record) ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_INVALID_STATE;
}

static int sequence_newer(uint32_t left, uint32_t right)
{
    return left != right && (int32_t)(left - right) > 0;
}

static firmware_status_t read_latest(update_journal_record_t *record, uint32_t *active_slot)
{
    update_journal_record_t slots[PLATFORM_JOURNAL_SLOT_COUNT];
    firmware_status_t status[PLATFORM_JOURNAL_SLOT_COUNT];
    uint32_t valid_count = 0U;
    uint32_t valid_slot = 0U;
    uint32_t slot;

    for (slot = 0U; slot < PLATFORM_JOURNAL_SLOT_COUNT; ++slot)
    {
        status[slot] = read_slot(slot, &slots[slot]);
        if (status[slot] == FIRMWARE_STATUS_OK)
        {
            ++valid_count;
            valid_slot = slot;
        }
    }

    if (valid_count == 0U)
    {
        if (status[0] == FIRMWARE_STATUS_NOT_FOUND && status[1] == FIRMWARE_STATUS_NOT_FOUND)
            return FIRMWARE_STATUS_NOT_FOUND;
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (valid_count == 1U)
    {
        *record = slots[valid_slot];
        if (active_slot != NULL)
            *active_slot = valid_slot;
        return FIRMWARE_STATUS_OK;
    }

    if (slots[0].sequence == slots[1].sequence)
    {
        if (memcmp(&slots[0], &slots[1], sizeof(slots[0])) != 0)
            return FIRMWARE_STATUS_INVALID_STATE;
        valid_slot = 0U;
    }
    else
        valid_slot = sequence_newer(slots[1].sequence, slots[0].sequence) ? 1U : 0U;

    *record = slots[valid_slot];
    if (active_slot != NULL)
        *active_slot = valid_slot;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateJournal_Read(update_journal_record_t *record)
{
    if (record == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return read_latest(record, NULL);
}

firmware_status_t UpdateJournal_WriteState(update_state_t state, update_target_t target)
{
    update_journal_record_t latest;
    update_journal_record_t next;
    update_journal_record_t verified;
    uint32_t active_slot = 1U;
    firmware_status_t status;

    if (!state_target_valid((uint32_t)state, (uint32_t)target))
        return FIRMWARE_STATUS_INVALID_STATE;

    status = read_latest(&latest, &active_slot);
    if (status != FIRMWARE_STATUS_OK && status != FIRMWARE_STATUS_NOT_FOUND)
        return status;

    (void) memset(&next, 0, sizeof(next));
    next.magic = UPDATE_JOURNAL_MAGIC;
    next.format_version = UPDATE_JOURNAL_FORMAT_VERSION;
    next.record_size = sizeof(next);
    next.sequence = status == FIRMWARE_STATUS_OK ? latest.sequence + 1U : 1U;
    next.state = (uint32_t)state;
    next.target = (uint32_t)target;
    next.crc32 = journal_crc(&next);

    status = PlatformJournalStorage_Write(active_slot ^ 1U, &next, sizeof(next));
    if (FirmwareStatus_IsError(status))
        return status;
    status = read_slot(active_slot ^ 1U, &verified);
    if (FirmwareStatus_IsError(status) || memcmp(&next, &verified, sizeof(next)) != 0)
        return FIRMWARE_STATUS_IO_ERROR;
    return FIRMWARE_STATUS_OK;
}
