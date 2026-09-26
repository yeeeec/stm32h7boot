#ifndef FIRMWARE_UPDATE_JOURNAL_H
#define FIRMWARE_UPDATE_JOURNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UPDATE_JOURNAL_MAGIC          0x55524A4CUL
#define UPDATE_JOURNAL_FORMAT_VERSION 1U

typedef enum
{
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_PENDING,
    UPDATE_STATE_WRITING,
    UPDATE_STATE_JUMPING
} update_state_t;

typedef enum
{
    UPDATE_TARGET_NONE = 0,
    UPDATE_TARGET_UPDATE,
    UPDATE_TARGET_ROLLBACK
} update_target_t;

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t record_size;
    uint32_t sequence;
    uint32_t state;
    uint32_t target;
    uint32_t crc32;
} update_journal_record_t;

_Static_assert(sizeof(update_journal_record_t) == 24U,
               "update journal ABI must remain fixed");

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_UPDATE_JOURNAL_H */
