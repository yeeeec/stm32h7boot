/**
 * @file bsp.h
 * @brief 将生成的 HAL 外设一次性绑定到板级设备。
 *
 * BSP 持有具体的板级设备实例并将其提供给 Adapter。BSP 不实现 Boot 或 Update
 * Policy；这些决策保留在硬件边界之上。
 */
#ifndef BSP_H
#define BSP_H

/* Bare-metal boot BSP owns CubeMX peripheral startup and concrete device
 * bindings. Display/SDRAM/USB/timer/HW-CRC resources belong to the app and are
 * deliberately not initialized by this target. */

#include "firmware/status.h"

/**
 * @brief 初始化 Firmware 使用的板级外设。
 *
 * 按 SDRAM、外部 QSPI Flash、配置的 AT24C128 EEPROM 顺序初始化。生成的
 * UART、FMC、QSPI 和 I2C Handle 必须已经由 CubeMX 启动代码初始化。
 * 从 MCU 的 System Memory Bootloader 不在此处自动初始化；调用者应在本函数
 * 成功后，使用已确认的 Device ID 调用 BSP_Stm32RomBootInit()，避免无目标型号
 * 校验地进入升级流程。
 *
 * 初始化没有 rollback。任何失败都是当前 Boot 的终态；调用者不得使用 BSP
 * 设备，也不得在系统 Reset 前重试。
 *
 * @pre 生成的 HAL 外设初始化已经完成。
 * @pre 当前 Boot 中尚未调用过本函数。
 *
 * @return 所有必需板级设备就绪后返回 FIRMWARE_STATUS_OK。
 * @return 初始化顺序、HAL Handle 或已保留板级设备无效时返回
 *         FIRMWARE_STATUS_INVALID_STATE。
 * @return 其他设备或传输失败时返回对应状态。
 */
firmware_status_t BSP_Init(void);

/**
 * @brief 查询 BSP_Init 是否成功完成。
 *
 * @return 仅在所有必需板级设备初始化后返回非零；初始化前或任意一次失败后
 *         返回零。
 */
int BSP_IsInitialized(void);

#endif
