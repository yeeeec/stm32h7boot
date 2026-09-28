#ifndef PLATFORM_JOURNAL_STORAGE_H
#define PLATFORM_JOURNAL_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "platform/platform_memory_map.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* A/B each own one of the final two 128 KiB internal-flash sectors. */
#define PLATFORM_JOURNAL_SLOT_SIZE  (128UL * 1024UL)

firmware_status_t PlatformJournalStorage_Init(void);
firmware_status_t PlatformJournalStorage_Read(uint32_t slot, void *data, size_t size);
firmware_status_t PlatformJournalStorage_Write(uint32_t slot, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_JOURNAL_STORAGE_H */
