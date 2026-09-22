#ifndef FIRMWARE_UPDATE_JOURNAL_H
#define FIRMWARE_UPDATE_JOURNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UPDATE_JOURNAL_MAGIC                       0x55524A4CUL
#define UPDATE_JOURNAL_FORMAT_VERSION              2U
#define UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING (1UL << 0U)

#define UPDATE_STATE_IDLE       0U
#define UPDATE_STATE_REQUESTED  1U
#define UPDATE_STATE_INSTALLING 2U
#define UPDATE_STATE_JUMPING    3U
#define UPDATE_STATE_FAILED     4U

typedef enum
{
    UPDATE_SOURCE_NONE = 0U,
    UPDATE_SOURCE_CANDIDATE = 1U,
    UPDATE_SOURCE_ROLLBACK = 2U
} update_source_t;

#define UPDATE_FLAG_CURRENT_COMMIT_PENDING UPDATE_JOURNAL_FLAG_CURRENT_COMMIT_PENDING

    typedef struct
    {
        uint32_t magic;
        uint32_t format_version;
        uint32_t sequence;
        uint32_t state;
        uint32_t source;
        uint32_t flags;
        uint32_t install_attempts;
        uint32_t jump_attempts;
        uint32_t commit_attempts;
        uint32_t last_error;
        uint32_t candidate_version;
        uint32_t component_mask;
        char candidate_package_id[64];
        uint8_t candidate_manifest_sha256[32];
        uint8_t running_manifest_sha256[32];
        uint32_t crc32;
    } update_journal_record_t;

    _Static_assert(sizeof(update_journal_record_t) == 180U, "update journal ABI must remain fixed");

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_UPDATE_JOURNAL_H */
