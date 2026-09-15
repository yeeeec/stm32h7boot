/**
 * @file bsp_external_flash.h
 * @brief 外部 SPI NOR Flash 的板级 QSPI 绑定。
 */
#ifndef BSP_EXTERNAL_FLASH_H
#define BSP_EXTERNAL_FLASH_H

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct spi_nor;

/**
 * @brief 初始化板级外部 SPI NOR Flash。
 *
 * @return 设备识别并就绪时返回 FIRMWARE_STATUS_OK。
 * @return CubeMX QSPI Handle 处于 Reset 时返回 FIRMWARE_STATUS_INVALID_STATE。
 * @return 其他情况返回驱动或传输失败状态。
 *
 * @pre 生成的 QSPI Handle 已完成初始化。
 * @pre 外部 Flash 驱动尚未初始化。
 */
firmware_status_t BSP_ExternalFlashInit(void);

/**
 * @brief 返回已初始化的外部 Flash 驱动实例。
 *
 * @return BSP 持有的 SPI NOR 实例；成功初始化前返回 NULL。
 *
 * @note 返回对象为静态生命周期，调用者不得释放或重新初始化。
 */
struct spi_nor *BSP_ExternalFlashDevice(void);

#ifdef __cplusplus
}
#endif

#endif
