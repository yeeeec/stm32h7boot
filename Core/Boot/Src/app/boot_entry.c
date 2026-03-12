#include "boot/boot_entry.h"

#include "boot/boot_config.h"
#include "boot/boot_diag.h"
#include "boot/boot_flow.h"
#include "boot/boot_ports.h"
#include "boot/boot_visual.h"

static boot_context_t s_boot_ctx;
static uint8_t s_work_buffer[BOOT_CFG_WORK_BUFFER_BYTES];

void boot_entry_init(void)
{
    uint32_t now_ms = boot_port_get_time_ms();

    boot_diag_init();
    boot_visual_init();
    boot_context_init(&s_boot_ctx, s_work_buffer, (uint32_t)sizeof(s_work_buffer), now_ms);
    boot_flow_init(&s_boot_ctx);
}

void boot_entry_process(void)
{
    boot_diag_snapshot_t snapshot;
    boot_result_t result = boot_flow_step(&s_boot_ctx);

    snapshot.stage = s_boot_ctx.stage;
    snapshot.last_result = result;
    snapshot.gap_mask = boot_diag_get_gap_mask();
    snapshot.timestamp_ms = boot_port_get_time_ms();
    boot_diag_set_snapshot(&snapshot);
}

const boot_context_t *boot_entry_get_context(void)
{
    return &s_boot_ctx;
}
