/**
 * @file runtime_layout.h
 * @brief Fixed APP and GUI runtime layout.
 */
#ifndef SERVICES_RUNTIME_LAYOUT_H
#define SERVICES_RUNTIME_LAYOUT_H

#include <stdint.h>

#define BOOT_APP_FLASH_OFFSET   0x000000UL
#define BOOT_APP_RUNTIME_BASE   0x90000000UL
#define BOOT_APP_RUNTIME_SIZE   (1UL * 1024UL * 1024UL)
#define BOOT_APP_RUNTIME_OFFSET BOOT_APP_FLASH_OFFSET

#define BOOT_GUI_FLASH_OFFSET   0x200000UL
#define BOOT_GUI_RUNTIME_BASE   0x90200000UL
#define BOOT_GUI_RUNTIME_SIZE   (8UL * 1024UL * 1024UL)
#define BOOT_GUI_RUNTIME_OFFSET BOOT_GUI_FLASH_OFFSET

typedef struct
{
    uint32_t app_offset;
    uint32_t app_max_size;
    uint32_t app_xip_base;
    uint32_t gui_offset;
    uint32_t gui_max_size;
    uint32_t gui_mmap_base;
} boot_runtime_layout_t;

extern const boot_runtime_layout_t BOOT_RUNTIME_LAYOUT;

const boot_runtime_layout_t *BootRuntimeLayout_Get(void);

#endif
