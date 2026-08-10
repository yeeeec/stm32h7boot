/**
 * @file stm32_rom_boot_programmer_adapter.c
 * @brief Stm32RomBoot 到 mcu_programmer_t 的协议边界。
 *
 * Adapter 只转换类型和会话生命周期，不实现镜像分块、重试或升级策略。
 */
#include "adapters/stm32_rom_boot_programmer_adapter.h"

#include <stddef.h>

#include "stm32_rom_boot.h"

/** 将 ROM Boot 的 GetInfo 能力复制为通用编程器能力。 */
static firmware_status_t Begin(void *context, mcu_programmer_info_t *info)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;
    stm32_rom_boot_info_t rom_info;
    firmware_status_t status;

    if ((adapter == NULL) || (adapter->device == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = Stm32RomBoot_Enter(adapter->device);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Stm32RomBoot_GetInfo(adapter->device, &rom_info);
    if (!FirmwareStatus_IsOk(status))
    {
        /* GetInfo 失败也必须把 BOOT/RST/UART 恢复到应用条件。 */
        (void) Stm32RomBoot_Leave(adapter->device);
        return status;
    }

    info->device_id      = rom_info.device_id;
    info->max_write_size = STM32_ROM_BOOT_MAX_MEMORY_TRANSFER;
    info->max_read_size  = STM32_ROM_BOOT_MAX_MEMORY_TRANSFER;
    info->max_erase_block_count =
        (adapter->device->config.erase_mode == STM32_ROM_BOOT_ERASE_EXTENDED)
            ? STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT
            : 255U;
    info->capabilities = 0U;
    if (Stm32RomBoot_IsCommandSupported(adapter->device, STM32_ROM_BOOT_COMMAND_READ_MEMORY) != 0)
    {
        info->capabilities |= MCU_PROGRAMMER_CAPABILITY_READ;
    }
    if (Stm32RomBoot_IsCommandSupported(adapter->device, STM32_ROM_BOOT_COMMAND_WRITE_MEMORY) != 0)
    {
        info->capabilities |= MCU_PROGRAMMER_CAPABILITY_WRITE;
    }
    if (Stm32RomBoot_IsCommandSupported(
            adapter->device, (adapter->device->config.erase_mode == STM32_ROM_BOOT_ERASE_EXTENDED)
                                 ? STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE
                                 : STM32_ROM_BOOT_COMMAND_ERASE) != 0)
    {
        info->capabilities |= MCU_PROGRAMMER_CAPABILITY_ERASE;
    }
    adapter->session_active = 1;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Erase(void *context, uint32_t page_start, uint32_t page_count)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;

    if ((adapter == NULL) || (adapter->device == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((page_start > UINT16_MAX) || (page_count == 0U) ||
        (page_count > UINT16_MAX))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return Stm32RomBoot_ErasePages(adapter->device, (uint16_t) page_start, (uint16_t) page_count);
}

static firmware_status_t Write(void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;

    if ((adapter == NULL) || (adapter->device == NULL) || (data == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return Stm32RomBoot_WriteMemory(adapter->device, address, data, size);
}

static firmware_status_t Read(void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;

    if ((adapter == NULL) || (adapter->device == NULL) || (data == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return Stm32RomBoot_ReadMemory(adapter->device, address, data, size);
}

static firmware_status_t End(void *context)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status                  = Stm32RomBoot_Leave(adapter->device);
    adapter->session_active = 0;
    return status;
}

static firmware_status_t Abort(void *context)
{
    stm32_rom_boot_programmer_adapter_t *adapter = (stm32_rom_boot_programmer_adapter_t *) context;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->session_active == 0)
    {
        return FIRMWARE_STATUS_OK;
    }
    status                  = Stm32RomBoot_Leave(adapter->device);
    adapter->session_active = 0;
    return status;
}

firmware_status_t Stm32RomBootProgrammerAdapter_Init(stm32_rom_boot_programmer_adapter_t *adapter,
                                                     struct stm32_rom_boot *device)
{
    if ((adapter == NULL) || (device == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    adapter->device            = device;
    adapter->session_active    = 0;
    adapter->interface.context = adapter;
    adapter->interface.begin   = Begin;
    adapter->interface.erase   = Erase;
    adapter->interface.write   = Write;
    adapter->interface.read    = Read;
    adapter->interface.end     = End;
    adapter->interface.abort   = Abort;
    return FIRMWARE_STATUS_OK;
}

const mcu_programmer_t *
Stm32RomBootProgrammerAdapter_Interface(const stm32_rom_boot_programmer_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
