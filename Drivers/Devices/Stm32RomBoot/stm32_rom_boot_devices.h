#ifndef STM32_ROM_BOOT_DEVICES_H
#define STM32_ROM_BOOT_DEVICES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define STM32_ROM_BOOT_DEVICE_QUIRK_NONE                       0x00000000UL
#define STM32_ROM_BOOT_DEVICE_QUIRK_H743_V91_BANK2_ERASE_GUARD 0x00000001UL
#define STM32_ROM_BOOT_NO_BANK2_FIRST_UNIT                     0xFFFFU

    typedef enum
    {
        STM32_ROM_BOOT_TARGET_STM32H743XI = 0,
        STM32_ROM_BOOT_TARGET_STM32F103C8,
        STM32_ROM_BOOT_TARGET_COUNT
    } stm32_rom_boot_target_t;

    typedef enum
    {
        STM32_ROM_BOOT_ERASE_STANDARD = 0,
        STM32_ROM_BOOT_ERASE_EXTENDED
    } stm32_rom_boot_erase_method_t;

    typedef struct
    {
        stm32_rom_boot_target_t target;
        const char *name;
        uint16_t device_id;
        uint32_t flash_base;
        uint32_t flash_size;
        uint32_t erase_unit_size;
        uint16_t erase_unit_count;
        uint16_t bank2_first_erase_unit;
        uint16_t write_alignment;
        stm32_rom_boot_erase_method_t erase_method;
        uint32_t quirks;
    } stm32_rom_boot_device_profile_t;

    const stm32_rom_boot_device_profile_t *Stm32RomBootDevices_Get(stm32_rom_boot_target_t target);

#ifdef __cplusplus
}
#endif

#endif