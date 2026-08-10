/**
 * @file stm32_rom_boot_programmer_adapter_test.c
 * @brief ROM Boot Adapter 的生命周期和参数转换测试。
 *
 * 本测试用链接级假驱动隔离 AN3155 帧细节，专门验证 Adapter 到通用接口的边界。
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "adapters/stm32_rom_boot_programmer_adapter.h"
#include "stm32_rom_boot.h"

static uint32_t enter_count;
static uint32_t info_count;
static uint32_t erase_count;
static uint32_t write_count;
static uint32_t read_count;
static uint32_t leave_count;

firmware_status_t Stm32RomBoot_Enter(stm32_rom_boot_t *device)
{
    enter_count++;
    device->in_bootloader = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_GetInfo(
    stm32_rom_boot_t *device,
    stm32_rom_boot_info_t *info)
{
    (void)device;
    info_count++;
    memset(info, 0, sizeof(*info));
    info->device_id = 0x0450U;
    info->command_count = 3U;
    info->commands[0] = STM32_ROM_BOOT_COMMAND_READ_MEMORY;
    info->commands[1] = STM32_ROM_BOOT_COMMAND_WRITE_MEMORY;
    info->commands[2] = STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE;
    return FIRMWARE_STATUS_OK;
}

int Stm32RomBoot_IsCommandSupported(
    const stm32_rom_boot_t *device,
    uint8_t command)
{
    (void)device;
    return (command == STM32_ROM_BOOT_COMMAND_READ_MEMORY) ||
           (command == STM32_ROM_BOOT_COMMAND_WRITE_MEMORY) ||
           (command == STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE);
}

firmware_status_t Stm32RomBoot_ErasePages(
    stm32_rom_boot_t *device,
    uint16_t page_start,
    uint16_t page_count)
{
    (void)device;
    assert(page_start == 4U);
    assert(page_count == 2U);
    erase_count++;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_WriteMemory(
    stm32_rom_boot_t *device,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    (void)device;
    assert(address == 0x08010000UL);
    assert(data != NULL);
    assert(size == 3U);
    write_count++;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_ReadMemory(
    stm32_rom_boot_t *device,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    (void)device;
    assert(address == 0x08010000UL);
    assert(data != NULL);
    assert(size == 3U);
    memset(data, 0xA5, size);
    read_count++;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_Leave(stm32_rom_boot_t *device)
{
    leave_count++;
    device->in_bootloader = 0;
    return FIRMWARE_STATUS_OK;
}

int main(void)
{
    stm32_rom_boot_t device = {0};
    stm32_rom_boot_programmer_adapter_t adapter = {0};
    mcu_programmer_info_t info = {0};
    mcu_programmer_t const *programmer;
    uint8_t data[3] = {1U, 2U, 3U};

    device.initialized = 1;
    device.config.erase_mode = STM32_ROM_BOOT_ERASE_EXTENDED;
    assert(Stm32RomBootProgrammerAdapter_Init(&adapter, &device) ==
           FIRMWARE_STATUS_OK);
    programmer = Stm32RomBootProgrammerAdapter_Interface(&adapter);
    assert(programmer != NULL);
    assert(programmer->begin(programmer->context, &info) == FIRMWARE_STATUS_OK);
    assert(info.device_id == 0x0450U);
    assert(info.max_write_size == STM32_ROM_BOOT_MAX_MEMORY_TRANSFER);
    assert(info.max_erase_block_count == STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT);
    assert((info.capabilities & MCU_PROGRAMMER_CAPABILITY_READ) != 0U);
    assert((info.capabilities & MCU_PROGRAMMER_CAPABILITY_WRITE) != 0U);
    assert((info.capabilities & MCU_PROGRAMMER_CAPABILITY_ERASE) != 0U);
    assert(programmer->erase(programmer->context, 4U, 2U) == FIRMWARE_STATUS_OK);
    assert(programmer->write(
               programmer->context, 0x08010000UL, data, sizeof(data)) ==
           FIRMWARE_STATUS_OK);
    assert(programmer->read(
               programmer->context, 0x08010000UL, data, sizeof(data)) ==
           FIRMWARE_STATUS_OK);
    assert(programmer->end(programmer->context) == FIRMWARE_STATUS_OK);
    assert((enter_count == 1U) && (info_count == 1U) && (erase_count == 1U) &&
           (write_count == 1U) && (read_count == 1U) && (leave_count == 1U));
    return 0;
}
