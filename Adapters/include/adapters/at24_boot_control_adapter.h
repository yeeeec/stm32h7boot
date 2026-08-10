/**
 * @file at24_boot_control_adapter.h
 * @brief 将 AT24 驱动适配为 Boot Control 持久化存储接口。
 */
#ifndef ADAPTERS_AT24_BOOT_CONTROL_ADAPTER_H
#define ADAPTERS_AT24_BOOT_CONTROL_ADAPTER_H

#include "firmware/boot_control_store.h"

struct at24;

/** 绑定调用者持有的 AT24 驱动实例的 Boot Control 接口。 */
typedef struct
{
    /** 对外暴露的 Boot Control 存储回调表。 */
    boot_control_store_t interface;
    /** 调用者持有的 AT24 设备引用。 */
    struct at24 *device;
} at24_boot_control_adapter_t;

/**
 * @brief 将已初始化的 AT24 设备绑定到 Boot Control 存储接口。
 *
 * @param[out] adapter 待初始化的适配器对象。
 * @param[in] device 已初始化的设备；适配器仅保存其引用。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 参数为 NULL 时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 * @return @p device 未初始化时返回底层驱动的状态错误。
 */
firmware_status_t At24BootControlAdapter_Init(
    at24_boot_control_adapter_t *adapter,
    struct at24 *device);

/** 返回适配器持有的 Boot Control 存储接口；参数为 NULL 时返回 NULL。 */
const boot_control_store_t *At24BootControlAdapter_Interface(
    const at24_boot_control_adapter_t *adapter);

#endif
