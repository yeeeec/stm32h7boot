#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_types.h"

typedef enum
{
    BOOT_DIAG_EVT_STAGE_ENTER = 0,
    BOOT_DIAG_EVT_STAGE_EXIT,
    BOOT_DIAG_EVT_WARN,
    BOOT_DIAG_EVT_ERROR,
    BOOT_DIAG_EVT_GAP_FOUND
} boot_diag_event_id_t;

enum
{
    BOOT_GAP_NONE = 0,
    BOOT_GAP_PORT_USB_LOADER = (1UL << 0),
    BOOT_GAP_PORT_IMAGE_VERIFY = (1UL << 1),
    BOOT_GAP_PORT_FLASH_PROGRAM = (1UL << 2),
    BOOT_GAP_PORT_XIP_MAP = (1UL << 3),
    BOOT_GAP_PORT_APP_JUMP = (1UL << 4),
    BOOT_GAP_MPU_CONFIG = (1UL << 5),
    BOOT_GAP_CACHE_CONFIG = (1UL << 6)
};

typedef struct
{
    uint32_t timestamp_ms;
    boot_stage_t stage;
    boot_diag_event_id_t event_id;
    int32_t detail;
} boot_diag_event_t;

typedef struct
{
    boot_stage_t stage;
    boot_result_t last_result;
    uint32_t gap_mask;
    uint32_t timestamp_ms;
} boot_diag_snapshot_t;

void boot_diag_init(void);
void boot_diag_push_event(boot_stage_t stage, boot_diag_event_id_t event_id, int32_t detail, uint32_t timestamp_ms);
void boot_diag_report_gap(uint32_t gap_mask, boot_stage_t stage, uint32_t timestamp_ms);
uint32_t boot_diag_get_gap_mask(void);
uint32_t boot_diag_copy_events(boot_diag_event_t *out_events, uint32_t max_events);
void boot_diag_set_snapshot(const boot_diag_snapshot_t *snapshot);
void boot_diag_get_snapshot(boot_diag_snapshot_t *snapshot);

#endif /* BOOT_DIAG_H */
