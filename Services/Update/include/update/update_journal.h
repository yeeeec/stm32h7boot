#ifndef FIRMWARE_UPDATE_JOURNAL_H
#define FIRMWARE_UPDATE_JOURNAL_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define UPDATE_JOURNAL_MAGIC          0x55524A4CUL
#define UPDATE_JOURNAL_FORMAT_VERSION 1U

    typedef enum
    {
        UPDATE_JOURNAL_IDLE = 0,
        UPDATE_JOURNAL_INSTALLING,
        UPDATE_JOURNAL_COMMITTING
    } update_journal_state_t;

    typedef struct
    {
        uint32_t magic;
        uint32_t format_version;
        uint32_t sequence;
        update_journal_state_t state;
        uint32_t component;
        uint32_t image_offset;
        uint32_t active_slot;
        uint32_t crc32;
    } update_journal_record_t;

    firmware_status_t UpdateJournal_Read(update_journal_record_t *record);
    firmware_status_t UpdateJournal_Write(const update_journal_record_t *record);
    firmware_status_t UpdateJournal_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_UPDATE_JOURNAL_H */
