/**
 * @file bsp_stm32_rom_boot.h
 * @brief STM32 从 MCU System Memory Bootloader 的板级绑定。
 *
 * 本模块把可移植的 Stm32RomBoot 驱动绑定到本板硬件：USART2、PC7 BOOT
 * 和 PG14 RST。BOOT 与 RST 均为高电平有效。BSP 只负责硬件绑定和协议
 * 基础参数，不负责固件镜像选择、地址范围、版本回退或升级结果发布。
 */
#ifndef BSP_STM32_ROM_BOOT_H
#define BSP_STM32_ROM_BOOT_H

#include <stdint.h>

#include "firmware/status.h"

struct stm32_rom_boot;

/** 选择目标 STM32 ROM Bootloader 支持的 Flash 擦除命令格式。 */
typedef enum
{
    /** 使用 AN3155 的标准 Erase Memory 命令，页号宽度为 8 bit。 */
    BSP_STM32_ROM_BOOT_ERASE_STANDARD = 0,
    /** 使用 Extended Erase Memory 命令，页号宽度为 16 bit。 */
    BSP_STM32_ROM_BOOT_ERASE_EXTENDED
} bsp_stm32_rom_boot_erase_mode_t;

/** 从 MCU 的板级固定配置。 */
typedef struct
{
    /**
     * Get ID 应返回的 STM32 Device ID。
     *
     * 必须填写目标芯片参考手册或 AN2606 中确认的实际 ID。BSP 拒绝驱动层
     * 用于实验探测的 0xFFFF，防止固件被写入错误型号的 MCU。
     */
    uint16_t expected_device_id;
    /** 根据目标 ROM 的命令集选择标准擦除或扩展擦除。 */
    bsp_stm32_rom_boot_erase_mode_t erase_mode;
} bsp_stm32_rom_boot_config_t;

/**
 * @brief 将 USART2、PC7 BOOT 和 PG14 RST 绑定到 ROM Boot 驱动。
 *
 * 初始化会先把 BOOT、RST 都置低，使从 MCU 保持“正常启动、未复位”状态，
 * 再把 USART2 恢复为应用通信使用的 115200 8N1。进入 ROM 时驱动会临时把
 * USART2 切换成逻辑 8E1，退出时自动恢复为 8N1。
 *
 * @param[in] config 已确认的目标 Device ID 与擦除命令模式；函数会复制配置。
 *
 * @return FIRMWARE_STATUS_OK 绑定完成。
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT 配置为空、Device ID 为 0xFFFF，
 *         或擦除模式无效。
 * @return FIRMWARE_STATUS_INVALID_STATE USART2 尚未由 CubeMX 初始化，或本 BSP
 *         实例已经初始化。
 * @return FIRMWARE_STATUS_IO_ERROR USART2 中止、反初始化或重新初始化失败。
 *
 * @pre MX_GPIO_Init() 与 MX_USART2_UART_Init() 已成功执行。
 * @note 本模块独占 USART2；初始化后其他模块不得并发收发或修改 USART2 配置。
 */
firmware_status_t BSP_Stm32RomBootInit(const bsp_stm32_rom_boot_config_t *config);

/**
 * @brief 获取 BSP 持有的 STM32 ROM Boot 驱动实例。
 *
 * @return 初始化成功后返回静态生命周期的驱动对象；初始化前返回 NULL。
 * @note 调用者不得释放该对象，也不得再次调用 Stm32RomBoot_Init()。
 */
struct stm32_rom_boot *BSP_Stm32RomBootDevice(void);

#endif
