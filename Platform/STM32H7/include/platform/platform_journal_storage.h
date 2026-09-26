#ifndef PLATFORM_JOURNAL_STORAGE_H
#define PLATFORM_JOURNAL_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Each A/B record has its own STM32H7 internal-flash erase sector. */
#define PLATFORM_JOURNAL_SLOT_COUNT 2U
#define PLATFORM_JOURNAL_SLOT_SIZE  (128UL * 1024UL)

firmware_status_t PlatformJournalStorage_Init(void);
firmware_status_t PlatformJournalStorage_Read(uint32_t slot, void *data, size_t size);
firmware_status_t PlatformJournalStorage_Write(uint32_t slot, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_JOURNAL_STORAGE_H */
