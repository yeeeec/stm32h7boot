#include "stm32_rom_boot_devices.h"

static const stm32_rom_boot_device_profile_t s_profiles[] = {
    {
        .target                 = STM32_ROM_BOOT_TARGET_STM32H743XI,
        .name                   = "STM32H743XI",
        .device_id              = 0x0450U,
        .flash_base             = 0x08000000UL,
        .flash_size             = 2UL * 1024UL * 1024UL,
        .erase_unit_size        = 128UL * 1024UL,
        .erase_unit_count       = 16U,
        .bank2_first_erase_unit = 8U,
        .write_alignment        = 4U,
        .erase_method           = STM32_ROM_BOOT_ERASE_EXTENDED,
        .quirks                 = STM32_ROM_BOOT_DEVICE_QUIRK_H743_V91_BANK2_ERASE_GUARD,
    },
    {
        .target                 = STM32_ROM_BOOT_TARGET_STM32F103C8,
        .name                   = "STM32F103C8",
        .device_id              = 0x0410U,
        .flash_base             = 0x08000000UL,
        .flash_size             = 64UL * 1024UL,
        .erase_unit_size        = 1UL * 1024UL,
        .erase_unit_count       = 64U,
        .bank2_first_erase_unit = STM32_ROM_BOOT_NO_BANK2_FIRST_UNIT,
        .write_alignment        = 4U,
        .erase_method           = STM32_ROM_BOOT_ERASE_STANDARD,
        .quirks                 = STM32_ROM_BOOT_DEVICE_QUIRK_NONE,
    },
};

const stm32_rom_boot_device_profile_t *Stm32RomBootDevices_Get(stm32_rom_boot_target_t target)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t) (sizeof(s_profiles) / sizeof(s_profiles[0])); ++i)
    {
        if (s_profiles[i].target == target)
        {
            return &s_profiles[i];
        }
    }

    return (const stm32_rom_boot_device_profile_t *) 0;
}