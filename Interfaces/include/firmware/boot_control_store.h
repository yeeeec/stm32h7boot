/**
 * @file boot_control_store.h
 * @brief Boot Control Record 使用的字节寻址持久化存储 Contract。
 */
#ifndef FIRMWARE_BOOT_CONTROL_STORE_H
#define FIRMWARE_BOOT_CONTROL_STORE_H

#include <stdint.h>

#include "firmware/status.h"

/** 以字节表达的 EEPROM 几何参数。 */
typedef struct
{
    uint32_t capacity_bytes; /**< 可按字节寻址的总容量。 */
    uint32_t page_size;      /**< 物理写页大小。 */
} boot_control_store_info_t;

/** 在发起地址或页边界请求前读取不变几何参数。 */
typedef firmware_status_t (*boot_control_store_get_info_fn)(
    void *context,
    boot_control_store_info_t *info);
/** 读取范围内的字节序列；目标 Buffer 仍由调用者持有。 */
typedef firmware_status_t (*boot_control_store_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
/**
 * 启动一次受页边界约束的写操作，通过 is_ready() 观察完成状态。
 * Provider 消费数据前，源 Buffer 必须保持有效。
 */
typedef firmware_status_t (*boot_control_store_write_page_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);
/**
 * 轮询最近启动的写操作。ready 非零前不允许其他操作；轮询失败不能解释为
 * 操作已完成。
 */
typedef firmware_status_t (*boot_control_store_is_ready_fn)(
    void *context,
    int *ready);

/**
 * @brief 通过轮询完成写操作的字节寻址持久化存储。
 *
 * Provider 持有 interface、context 和物理设备。调用者在 Provider 生命周期内
 * 借用它们，并持有所有读写 Buffer。
 */
typedef struct
{
    void *context;
    boot_control_store_get_info_fn get_info;
    boot_control_store_read_fn read;
    boot_control_store_write_page_fn write_page;
    boot_control_store_is_ready_fn is_ready;
} boot_control_store_t;

#endif
