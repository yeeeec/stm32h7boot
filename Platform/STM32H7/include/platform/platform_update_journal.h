#ifndef PLATFORM_UPDATE_JOURNAL_H
#define PLATFORM_UPDATE_JOURNAL_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    UPDATE_JOURNAL_IDLE = 0,
    UPDATE_JOURNAL_INSTALLING,
    UPDATE_JOURNAL_COMMITTING
} platform_update_journal_state_t;

typedef struct
{
    platform_update_journal_state_t state;
    uint32_t component;
    uint32_t offset;
    uint32_t crc;
} platform_update_journal_t;

firmware_status_t PlatformUpdateJournal_Read(platform_update_journal_t *journal);
firmware_status_t PlatformUpdateJournal_Write(const platform_update_journal_t *journal);
firmware_status_t PlatformUpdateJournal_Clear(void);

#endif /* PLATFORM_UPDATE_JOURNAL_H */
