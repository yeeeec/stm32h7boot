/**
 * @file runtime_layout.c
 * @brief Fixed APP and GUI runtime layout instance.
 */
#include "services/common/runtime_layout.h"

const boot_runtime_layout_t BOOT_RUNTIME_LAYOUT = {
    BOOT_APP_FLASH_OFFSET,
    BOOT_APP_RUNTIME_SIZE,
    BOOT_APP_RUNTIME_BASE,
    BOOT_GUI_FLASH_OFFSET,
    BOOT_GUI_RUNTIME_SIZE,
    BOOT_GUI_RUNTIME_BASE,
};

const boot_runtime_layout_t *BootRuntimeLayout_Get(void)
{
    return &BOOT_RUNTIME_LAYOUT;
}
