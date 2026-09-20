#include "update_internal.h"

#include <stddef.h>
#include <string.h>

#include "checksum/crc32.h"
#include "platform/platform_nv_storage.h"
#include "update_config.h"

#define UPDATE_JOURNAL_SLOT_COUNT 2U
#define UPDATE_JOURNAL_SLOT_SIZE ((uint32_t) sizeof(update_journal_record_t))

_Static_assert(sizeof(update_journal_record_t) == 52U,
               "update journal format must remain fixed");

static uint32_t journal_crc(const update_journal_record_t *record)
{
    return Checksum_Crc32Ieee((const uint8_t *) record,
                              offsetof(update_journal_record_t, crc32));
}

static int journal_valid(const update_journal_record_t *record)
{
    return record->magic == UPDATE_JOURNAL_MAGIC &&
           record->format_version == UPDATE_JOURNAL_FORMAT_VERSION &&
           record->phase <= UPDATE_PHASE_COMMITTING && record->crc32 == journal_crc(record);
}

static int journal_empty(const update_journal_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *) record;
    size_t index;
    int all_zero = 1;
    int all_erased = 1;

    for (index = 0U; index < sizeof(*record); ++index)
    {
        if (bytes[index] != 0U)
            all_zero = 0;
        if (bytes[index] != 0xFFU)
            all_erased = 0;
    }
    return all_zero || all_erased;
}

static firmware_status_t read_slot(uint32_t slot, update_journal_record_t *record)
{
    firmware_status_t status = PlatformNvStorage_Read(
        UPDATE_JOURNAL_STORAGE_OFFSET + slot * UPDATE_JOURNAL_SLOT_SIZE,
        record, sizeof(*record));

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
    firmware_status_t status;

    if (record == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = read_latest(record, NULL);
    if (FirmwareStatus_IsError(status))
        return status;
    return record->phase == UPDATE_PHASE_NONE ? FIRMWARE_STATUS_NOT_FOUND
                                               : FIRMWARE_STATUS_OK;
}

static firmware_status_t write_record(update_journal_record_t *stored, uint32_t target_slot)
{
    update_journal_record_t verified;
    firmware_status_t status;

    stored->crc32 = journal_crc(stored);
    status = PlatformNvStorage_Write(
        UPDATE_JOURNAL_STORAGE_OFFSET + target_slot * UPDATE_JOURNAL_SLOT_SIZE,
        stored, sizeof(*stored));
    if (FirmwareStatus_IsError(status))
        return status;
    status = read_slot(target_slot, &verified);
    if (FirmwareStatus_IsError(status) || memcmp(stored, &verified, sizeof(*stored)) != 0)
        return FIRMWARE_STATUS_IO_ERROR;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateJournal_Write(update_phase_t phase,
                                      const uint8_t manifest_sha256[32])
{
    update_journal_record_t latest;
    update_journal_record_t stored;
    uint32_t active_slot = 1U;
    firmware_status_t status;

    if (phase == UPDATE_PHASE_NONE || phase > UPDATE_PHASE_COMMITTING ||
        manifest_sha256 == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    (void) memset(&stored, 0, sizeof(stored));
    status = read_latest(&latest, &active_slot);
    stored.sequence = status == FIRMWARE_STATUS_OK ? latest.sequence + 1U : 1U;
    if (status != FIRMWARE_STATUS_OK && status != FIRMWARE_STATUS_NOT_FOUND)
        return status;
    stored.magic = UPDATE_JOURNAL_MAGIC;
    stored.format_version = UPDATE_JOURNAL_FORMAT_VERSION;
    stored.phase = phase;
    (void) memcpy(stored.manifest_sha256, manifest_sha256,
                  sizeof(stored.manifest_sha256));
    return write_record(&stored, active_slot ^ 1U);
}

firmware_status_t UpdateJournal_Clear(void)
{
    update_journal_record_t latest;
    update_journal_record_t cleared;
    uint32_t active_slot = 1U;
    firmware_status_t status = read_latest(&latest, &active_slot);

    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return FIRMWARE_STATUS_OK;
    if (FirmwareStatus_IsError(status))
        return status;

    (void) memset(&cleared, 0, sizeof(cleared));
    cleared.magic = UPDATE_JOURNAL_MAGIC;
    cleared.format_version = UPDATE_JOURNAL_FORMAT_VERSION;
    cleared.sequence = latest.sequence + 1U;
    cleared.phase = UPDATE_PHASE_NONE;
    return write_record(&cleared, active_slot ^ 1U);
}
