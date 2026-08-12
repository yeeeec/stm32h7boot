/**
 * @file stm32_qspi_xip_adapter.h
 * @brief STM32 QSPI memory-mapped 执行适配器。
 */
#ifndef ADAPTERS_STM32_QSPI_XIP_ADAPTER_H
#define ADAPTERS_STM32_QSPI_XIP_ADAPTER_H

#include "firmware/xip_controller.h"

/** 保存适配器持有的 XIP 接口及 memory-mapped 状态。 */
typedef struct
{
    /** 对外暴露的 XIP 控制器回调表。 */
    xip_controller_t interface;
    /** 调用者持有的 HAL QSPI 句柄引用。 */
    void *qspi_handle;
    /** 最近一次从硬件 CCR 读取的 memory-mapped 状态。 */
    int mapped;
} stm32_qspi_xip_adapter_t;

/**
 * @brief 将已初始化的 STM32 QSPI 句柄绑定到 XIP 接口。
 *
 * @param[out] adapter 待初始化的适配器对象。
 * @param[in] qspi_handle 已初始化的 HAL QSPI 句柄；适配器仅保存其引用。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 任一参数为 NULL 时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 */
firmware_status_t Stm32QspiXipAdapter_Init(stm32_qspi_xip_adapter_t *adapter, void *qspi_handle);

/** 返回适配器持有的 XIP 控制器接口；参数为 NULL 时返回 NULL。 */
const xip_controller_t *Stm32QspiXipAdapter_Interface(const stm32_qspi_xip_adapter_t *adapter);

#endif
