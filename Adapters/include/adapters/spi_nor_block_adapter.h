/**
 * @file spi_nor_block_adapter.h
 * @brief 将 SPI NOR 驱动适配为受限的异步块设备接口。
 */
#ifndef SPI_NOR_BLOCK_ADAPTER_H
#define SPI_NOR_BLOCK_ADAPTER_H

#include "firmware/async_block_device.h"

struct spi_nor;

/** 保存绑定调用者持有的 SPI NOR 设备的异步接口。 */
typedef struct
{
    /** 异步块设备回调表。 */
    async_block_device_t async_interface;
    /** 调用者持有的 SPI NOR 设备引用。 */
    struct spi_nor *device;
} spi_nor_block_adapter_t;

/**
 * @brief 将块设备适配器绑定到已初始化的 SPI NOR 设备。
 *
 * @param[out] adapter 待初始化的适配器实例。
 * @param[in] device SPI NOR 设备；适配器仅保存其引用。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 任一参数为 NULL 时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 *
 * @pre 适配器使用期间 @p device 必须保持有效。
 */
firmware_status_t SpiNorBlockAdapter_Init(
    spi_nor_block_adapter_t *adapter,
    struct spi_nor *device);

/** 返回受限的异步块设备接口；参数为 NULL 时返回 NULL。 */
const async_block_device_t *SpiNorBlockAdapter_AsyncInterface(
    const spi_nor_block_adapter_t *adapter);

#endif
