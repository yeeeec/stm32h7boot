/**
 * @file mcu_programmer.h
 * @brief 与具体 MCU ROM/调试协议无关的固件编程器接口。
 */
#ifndef FIRMWARE_MCU_PROGRAMMER_H
#define FIRMWARE_MCU_PROGRAMMER_H

#include <stdint.h>

#include "firmware/status.h"

/** 编程器能力位；Service 在首次擦写前检查全部需要的能力。 */
#define MCU_PROGRAMMER_CAPABILITY_READ  (1UL << 0U)
#define MCU_PROGRAMMER_CAPABILITY_WRITE (1UL << 1U)
#define MCU_PROGRAMMER_CAPABILITY_ERASE (1UL << 2U)

/** 目标 MCU 会话期间报告的设备能力。 */
typedef struct
{
    /** 目标 MCU 的原生 Device ID。 */
    uint16_t device_id;
    /** 单次 program/write 允许的最大字节数。 */
    uint32_t max_write_size;
    /** 单次 read 允许的最大字节数。 */
    uint32_t max_read_size;
    /** 单次 erase 允许提交的最大逻辑块数。 */
    uint32_t max_erase_block_count;
    /** MCU_PROGRAMMER_CAPABILITY_* 的组合。 */
    uint32_t capabilities;
} mcu_programmer_info_t;

/**
 * 进入目标编程会话并返回已探测的能力。若返回失败，Provider 必须自行清理
 * 可能已改变的 BOOT/RST/UART 条件，因为 Service 只会对成功 begin() 执行 abort。
 */
typedef firmware_status_t (*mcu_programmer_begin_fn)(void *context, mcu_programmer_info_t *info);
/** 擦除从 page_start 开始的连续逻辑块。页号含义由 Adapter 的目标硬件定义。 */
typedef firmware_status_t (*mcu_programmer_erase_fn)(void *context, uint32_t page_start,
                                                     uint32_t page_count);
/** 向目标绝对地址写入一段镜像数据。 */
typedef firmware_status_t (*mcu_programmer_write_fn)(void *context, uint32_t address,
                                                     const uint8_t *data, uint32_t size);
/** 从目标绝对地址读回一段数据。 */
typedef firmware_status_t (*mcu_programmer_read_fn)(void *context, uint32_t address, uint8_t *data,
                                                    uint32_t size);
/** 成功完成会话，恢复目标 MCU 的用户应用启动条件。 */
typedef firmware_status_t (*mcu_programmer_end_fn)(void *context);
/** 失败或取消时尽力退出会话并恢复目标 MCU；结果只用于诊断。 */
typedef firmware_status_t (*mcu_programmer_abort_fn)(void *context);

/**
 * @brief 目标 MCU 固件编程器。
 *
 * begin() 成功后才允许 erase/read/write/end/abort。每个回调都是一次同步、
 * 有界的协议事务；长镜像的分块和轮询由上层 Service 完成。
 */
typedef struct
{
    void *context;
    mcu_programmer_begin_fn begin;
    mcu_programmer_erase_fn erase;
    mcu_programmer_write_fn write;
    mcu_programmer_read_fn read;
    mcu_programmer_end_fn end;
    mcu_programmer_abort_fn abort;
} mcu_programmer_t;

#endif
