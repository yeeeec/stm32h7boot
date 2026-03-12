#include "boot/boot_porting.h"

#include <string.h>

#include "main.h"
#include "boot/boot_diag.h"
#include "boot/boot_ports.h"

void boot_porting_check_run(boot_porting_report_t *report)
{
    boot_port_capability_t capability;
    uint32_t gap_mask = 0U;

    if (report == 0)
    {
        return;
    }

    (void)memset(report, 0, sizeof(*report));
    (void)memset(&capability, 0, sizeof(capability));

    report->icache_on = ((SCB->CCR & SCB_CCR_IC_Msk) != 0U) ? 1U : 0U;
    report->dcache_on = ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) ? 1U : 0U;
    report->mpu_on = ((MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0U) ? 1U : 0U;
    report->vtor = SCB->VTOR;

    boot_port_get_capability(&capability);

    if (capability.usb_loader == 0U)
    {
        gap_mask |= BOOT_GAP_PORT_USB_LOADER;
    }
    if (capability.image_validator == 0U)
    {
        gap_mask |= BOOT_GAP_PORT_IMAGE_VERIFY;
    }
    if (capability.ext_flash_programmer == 0U)
    {
        gap_mask |= BOOT_GAP_PORT_FLASH_PROGRAM;
    }
    if (capability.xip_mapper == 0U)
    {
        gap_mask |= BOOT_GAP_PORT_XIP_MAP;
    }
    if (capability.app_jumper == 0U)
    {
        gap_mask |= BOOT_GAP_PORT_APP_JUMP;
    }
    if ((report->icache_on == 0U) || (report->dcache_on == 0U))
    {
        gap_mask |= BOOT_GAP_CACHE_CONFIG;
    }
    if (report->mpu_on == 0U)
    {
        gap_mask |= BOOT_GAP_MPU_CONFIG;
    }

    report->gap_mask = gap_mask;
}
