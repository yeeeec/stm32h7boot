/**
 * @file boot_types.h
 * @brief 各 Service API 共享的稳定 Bootloader 领域类型。
 *
 * 类型只表达业务分区、版本和内存区域，不暴露 HAL、Flash 驱动或文件系统类型。
 */
#ifndef SERVICES_BOOT_TYPES_H
#define SERVICES_BOOT_TYPES_H

#include <stdint.h>

typedef enum
{
    /** 可执行 Application Runtime 组件。 */
    BOOT_COMPONENT_APP = 0,
    /** 供 Application 使用的 GUI/资源 Runtime 组件。 */
    BOOT_COMPONENT_GUI = 1
} boot_component_t;

/** 三段式发布版本；比较规则由 VersionPolicy 能力提供。 */
typedef struct
{
    /** 主版本号。 */
    uint16_t major;
    /** 次版本号。 */
    uint16_t minor;
    /** 修订版本号。 */
    uint16_t patch;
} release_version_t;

/**
 * @brief 某个 Runtime 组件在外部 Flash 与 CPU 映射空间中的固定区域。
 *
 * flash_offset 用于 indirect 读写；mapped_address 用于 XIP 向量及地址校验；
 * capacity_bytes 是该组件可使用的总容量，而非当前镜像实际大小。
 */
typedef struct
{
    /** 相对于外部 Flash 起始位置的字节偏移。 */
    uint32_t flash_offset;
    /** QSPI memory-mapped CPU 地址。 */
    uint32_t mapped_address;
    /** 固定分区容量，单位为字节。 */
    uint32_t capacity_bytes;
} boot_region_t;

#endif
