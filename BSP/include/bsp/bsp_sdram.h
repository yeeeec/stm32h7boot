/**
 * @file bsp_sdram.h
 * @brief 通过 FMC Controller 初始化板级 SDRAM。
 */
#ifndef BSP_SDRAM_H
#define BSP_SDRAM_H

#include "firmware/status.h"

/**
 * @brief 初始化板级 SDRAM 时序和模式配置。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return CubeMX SDRAM Handle 处于 Reset 时返回 FIRMWARE_STATUS_INVALID_STATE。
 * @return 其他情况返回 Controller 或传输失败状态。
 *
 * @pre 生成的 FMC/SDRAM Handle 已完成初始化。
 * @pre 本函数成功前，调用者不得访问外部 SDRAM。
 */
firmware_status_t BSP_SdramInit(void);

#endif
