/**
 * @file runtime_layout.h
 * @brief 固定 APP/GUI Runtime 分区与 CPU 映射布局。
 *
 * 该布局是发布包、Flash 写入、哈希校验和 XIP 跳转之间的共同合同。任何地址或
 * 大小变更都必须同步更新 Application linker script 与发布工具，而不能仅修改
 * 某个 Adapter。
 */
#ifndef SERVICES_RUNTIME_LAYOUT_H
#define SERVICES_RUNTIME_LAYOUT_H

#include <stdint.h>

/** APP 在 W25Q256 中的固定起始偏移。 */
#define BOOT_APP_FLASH_OFFSET 0x000000UL
/** APP 在 QSPI memory-mapped 窗口中的 CPU 起始地址。 */
#define BOOT_APP_RUNTIME_BASE 0x90000000UL
/** APP 固定 Runtime 分区容量，单位为字节。 */
#define BOOT_APP_RUNTIME_SIZE (1UL * 1024UL * 1024UL)
/** 与 @ref BOOT_APP_FLASH_OFFSET 同义的历史兼容名称。 */
#define BOOT_APP_RUNTIME_OFFSET BOOT_APP_FLASH_OFFSET

/** GUI 在 W25Q256 中的固定起始偏移。 */
#define BOOT_GUI_FLASH_OFFSET 0x200000UL
/** GUI 在 QSPI memory-mapped 窗口中的 CPU 起始地址。 */
#define BOOT_GUI_RUNTIME_BASE 0x90200000UL
/** GUI 固定 Runtime 分区容量，单位为字节。 */
#define BOOT_GUI_RUNTIME_SIZE (8UL * 1024UL * 1024UL)
/** 与 @ref BOOT_GUI_FLASH_OFFSET 同义的历史兼容名称。 */
#define BOOT_GUI_RUNTIME_OFFSET BOOT_GUI_FLASH_OFFSET

/** 将固定宏布局作为可注入只读对象传递给 Service 的结构。 */
typedef struct
{
    /** APP 外部 Flash 偏移。 */
    uint32_t app_offset;
    /** APP 分区容量。 */
    uint32_t app_max_size;
    /** APP XIP CPU 地址。 */
    uint32_t app_xip_base;
    /** GUI 外部 Flash 偏移。 */
    uint32_t gui_offset;
    /** GUI 分区容量。 */
    uint32_t gui_max_size;
    /** GUI memory-mapped CPU 地址。 */
    uint32_t gui_mmap_base;
} boot_runtime_layout_t;

/** 唯一的固定 Runtime 布局实例；内容在运行期不可修改。 */
extern const boot_runtime_layout_t BOOT_RUNTIME_LAYOUT;

/**
 * @brief 返回固定布局实例。
 *
 * @return 永不为 NULL 的只读布局指针，其生命周期覆盖整个固件运行期。
 */
const boot_runtime_layout_t *BootRuntimeLayout_Get(void);

#endif
