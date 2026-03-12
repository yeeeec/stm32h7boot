#include "boot/boot_diag.h"

#include <string.h>

#include "boot/boot_config.h"

static boot_diag_event_t s_events[BOOT_CFG_TRACE_DEPTH];
static uint32_t s_event_head;
static uint32_t s_event_count;
static uint32_t s_gap_mask;
static boot_diag_snapshot_t s_snapshot;

void boot_diag_init(void)
{
    (void)memset(s_events, 0, sizeof(s_events));
    s_event_head = 0U;
    s_event_count = 0U;
    s_gap_mask = 0U;
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
}

void boot_diag_push_event(boot_stage_t stage, boot_diag_event_id_t event_id, int32_t detail, uint32_t timestamp_ms)
{
    boot_diag_event_t *slot = &s_events[s_event_head];

    slot->timestamp_ms = timestamp_ms;
    slot->stage = stage;
    slot->event_id = event_id;
    slot->detail = detail;

    s_event_head = (s_event_head + 1U) % BOOT_CFG_TRACE_DEPTH;
    if (s_event_count < BOOT_CFG_TRACE_DEPTH)
    {
        s_event_count++;
    }
}

void boot_diag_report_gap(uint32_t gap_mask, boot_stage_t stage, uint32_t timestamp_ms)
{
    if (gap_mask == 0U)
    {
        return;
    }

    s_gap_mask |= gap_mask;
    boot_diag_push_event(stage, BOOT_DIAG_EVT_GAP_FOUND, (int32_t)gap_mask, timestamp_ms);
}

uint32_t boot_diag_get_gap_mask(void)
{
    return s_gap_mask;
}

uint32_t boot_diag_copy_events(boot_diag_event_t *out_events, uint32_t max_events)
{
    uint32_t copied = 0U;
    uint32_t start = 0U;

    if ((out_events == 0) || (max_events == 0U) || (s_event_count == 0U))
    {
        return 0U;
    }

    if (s_event_count < BOOT_CFG_TRACE_DEPTH)
    {
        start = 0U;
    }
    else
    {
        start = s_event_head;
    }

    while ((copied < max_events) && (copied < s_event_count))
    {
        out_events[copied] = s_events[(start + copied) % BOOT_CFG_TRACE_DEPTH];
        copied++;
    }

    return copied;
}

void boot_diag_set_snapshot(const boot_diag_snapshot_t *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    s_snapshot = *snapshot;
}

void boot_diag_get_snapshot(boot_diag_snapshot_t *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    *snapshot = s_snapshot;
}
