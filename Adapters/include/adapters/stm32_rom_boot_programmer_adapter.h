/**
 * @file stm32_rom_boot_programmer_adapter.h
 * @brief 将 Stm32RomBoot 驱动适配为通用 MCU 编程器接口。
 */
#ifndef STM32_ROM_BOOT_PROGRAMMER_ADAPTER_H
#define STM32_ROM_BOOT_PROGRAMMER_ADAPTER_H

#include "firmware/mcu_programmer.h"

struct stm32_rom_boot;

/** Adapter 持有 BSP 驱动引用，不复制或释放驱动对象。 */
typedef struct
{
    /** 对外暴露的编程器回调表。 */
    mcu_programmer_t interface;
    /** BSP 初始化并持有的 STM32 ROM Boot 驱动。 */
    struct stm32_rom_boot *device;
    /** 当前是否已经成功进入 ROM Bootloader 会话。 */
    int session_active;
} stm32_rom_boot_programmer_adapter_t;

/**
 * @brief 绑定已初始化的 BSP ROM Boot 驱动。
 *
 * @param[out] adapter 调用者持有的静态 Adapter 对象。
 * @param[in] device BSP_Stm32RomBootDevice() 返回的驱动对象。
 * @return 参数有效时返回 FIRMWARE_STATUS_OK。
 */
firmware_status_t Stm32RomBootProgrammerAdapter_Init(
    stm32_rom_boot_programmer_adapter_t *adapter,
    struct stm32_rom_boot *device);

/** 返回 Adapter 内嵌的通用编程器接口；参数为空时返回 NULL。 */
const mcu_programmer_t *Stm32RomBootProgrammerAdapter_Interface(
    const stm32_rom_boot_programmer_adapter_t *adapter);

#endif
