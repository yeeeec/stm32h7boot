#ifndef FIRMWARE_UPDATE_SERVICE_H
#define FIRMWARE_UPDATE_SERVICE_H

#include "firmware/status.h"
#include "firmware/update_journal.h"
#include "update/update_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t UpdateService_Init(void);
    update_result_t UpdateService_Process(void);
    firmware_status_t UpdateJournal_Read(update_journal_record_t *record);
    firmware_status_t UpdateJournal_WriteState(update_state_t state, update_target_t target);

#ifdef __cplusplus
}
#endif

#endif
