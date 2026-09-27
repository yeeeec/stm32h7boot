#ifndef UPDATE_JOURNAL_INTERNAL_H
#define UPDATE_JOURNAL_INTERNAL_H

#include "firmware/status.h"
#include "firmware/update_journal.h"

firmware_status_t UpdateJournal_Read(update_journal_record_t *record);
firmware_status_t UpdateJournal_WriteState(update_state_t state, update_target_t target);
firmware_status_t UpdateJournal_ResetIdle(void);

#endif /* UPDATE_JOURNAL_INTERNAL_H */
