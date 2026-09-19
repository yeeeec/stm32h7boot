/**
 * @file stm32h743_rom_boot.c
 * @brief STM32H743XIH6 AN3155 UART ROM-bootloader host implementation.
 */
#include "stm32h743_rom_boot.h"

#include <stddef.h>

#define ROM_SYNC                 0x7FU
#define ROM_ACK                  0x79U
#define ROM_NACK                 0x1FU
#define ROM_CMD_GET              0x00U
#define ROM_CMD_GET_ID           0x02U
#define ROM_CMD_READ_MEMORY      0x11U
#define ROM_CMD_WRITE_MEMORY     0x31U
#define ROM_CMD_EXTENDED_ERASE   0x44U
#define ROM_VERSION_V91          0x91U
#define ROM_ERASE_FRAME_SIZE     (2U + (STM32H743_ROM_BOOT_SECTOR_COUNT * 2U) + 1U)

static uint8_t XorBytes(const uint8_t *data, uint32_t size)
{
    uint8_t value = 0U;
    uint32_t i;
    for (i = 0U; i < size; ++i)
    {
        value ^= data[i];
    }
    return value;
}

static void PutBe32(uint8_t data[4], uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void ClearInfo(stm32h743_rom_boot_info_t *info)
{
    uint32_t i;
    info->bootloader_version = 0U;
    info->device_id = 0xFFFFU;
    info->command_count = 0U;
    for (i = 0U; i < STM32H743_ROM_BOOT_MAX_COMMAND_COUNT; ++i)
    {
        info->commands[i] = 0U;
    }
}

static int HasCommand(const stm32h743_rom_boot_t *device, uint8_t command)
{
    uint32_t i;
    for (i = 0U; i < device->info.command_count; ++i)
    {
        if (device->info.commands[i] == command)
        {
            return 1;
        }
    }
    return 0;
}

static stm32h743_rom_boot_status_t Fault(stm32h743_rom_boot_t *device,
                                         stm32h743_rom_boot_status_t status)
{
    if (device != NULL)
    {
        device->session = STM32H743_ROM_BOOT_SESSION_FAULTED;
    }
    return status;
}

static stm32h743_rom_boot_status_t Tx(stm32h743_rom_boot_t *device, const uint8_t *data,
                                      uint32_t size, uint32_t timeout_ms)
{
    return device->port.transmit(device->port.context, data, size, timeout_ms);
}

static stm32h743_rom_boot_status_t Rx(stm32h743_rom_boot_t *device, uint8_t *data,
                                      uint32_t size, uint32_t timeout_ms)
{
    return device->port.receive(device->port.context, data, size, timeout_ms);
}

static stm32h743_rom_boot_status_t ReceiveAck(stm32h743_rom_boot_t *device,
                                               uint32_t timeout_ms)
{
    uint8_t response;
    stm32h743_rom_boot_status_t status = Rx(device, &response, 1U, timeout_ms);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    if (response == ROM_NACK)
    {
        return STM32H743_ROM_BOOT_STATUS_NACK;
    }
    return (response == ROM_ACK) ? STM32H743_ROM_BOOT_STATUS_OK
                                 : STM32H743_ROM_BOOT_STATUS_PROTOCOL_ERROR;
}

static stm32h743_rom_boot_status_t SendCommand(stm32h743_rom_boot_t *device,
                                                uint8_t command)
{
    uint8_t frame[2] = {command, (uint8_t)~command};
    stm32h743_rom_boot_status_t status =
        Tx(device, frame, sizeof(frame), device->config.command_timeout_ms);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    return ReceiveAck(device, device->config.command_timeout_ms);
}

static stm32h743_rom_boot_status_t SendAddress(stm32h743_rom_boot_t *device,
                                                uint32_t address)
{
    uint8_t frame[5];
    stm32h743_rom_boot_status_t status;
    PutBe32(frame, address);
    frame[4] = XorBytes(frame, 4U);
    status = Tx(device, frame, sizeof(frame), device->config.command_timeout_ms);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    return ReceiveAck(device, device->config.command_timeout_ms);
}

static stm32h743_rom_boot_status_t RequireSession(const stm32h743_rom_boot_t *device,
                                                   int ready)
{
    if (device == NULL)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    if (device->session == STM32H743_ROM_BOOT_SESSION_FAULTED)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    if (ready != 0)
    {
        return (device->session == STM32H743_ROM_BOOT_SESSION_READY)
                   ? STM32H743_ROM_BOOT_STATUS_OK
                   : STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    return (device->session == STM32H743_ROM_BOOT_SESSION_SYNCED)
               ? STM32H743_ROM_BOOT_STATUS_OK
               : STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
}

static stm32h743_rom_boot_status_t ValidateRange(uint32_t address, uint32_t size)
{
    if (size == 0U)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    if ((address < STM32H743_ROM_BOOT_FLASH_BASE) ||
        (address >= STM32H743_ROM_BOOT_FLASH_END_EXCLUSIVE) ||
        (size > (STM32H743_ROM_BOOT_FLASH_END_EXCLUSIVE - address)))
    {
        return STM32H743_ROM_BOOT_STATUS_OUT_OF_RANGE;
    }
    return STM32H743_ROM_BOOT_STATUS_OK;
}

static stm32h743_rom_boot_status_t RestoreApplication(stm32h743_rom_boot_t *device)
{
    stm32h743_rom_boot_status_t first = STM32H743_ROM_BOOT_STATUS_OK;
    stm32h743_rom_boot_status_t status;

    status = device->port.set_boot0(device->port.context, 0);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        first = status;
    }
    status = device->port.configure_uart(device->port.context,
                                         STM32H743_ROM_BOOT_UART_APPLICATION);
    if ((status != STM32H743_ROM_BOOT_STATUS_OK) &&
        (first == STM32H743_ROM_BOOT_STATUS_OK))
    {
        first = status;
    }
    status = device->port.set_reset(device->port.context, 1);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        device->port.delay_ms(device->port.context, device->config.reset_settle_ms);
        status = device->port.set_reset(device->port.context, 0);
    }
    if ((status != STM32H743_ROM_BOOT_STATUS_OK) &&
        (first == STM32H743_ROM_BOOT_STATUS_OK))
    {
        first = status;
    }
    device->session = STM32H743_ROM_BOOT_SESSION_CLOSED;
    return first;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Init(
    stm32h743_rom_boot_t *device, const stm32h743_rom_boot_port_t *port,
    const stm32h743_rom_boot_config_t *config)
{
    if ((device == NULL) || (port == NULL) || (config == NULL) ||
        (port->configure_uart == NULL) || (port->transmit == NULL) ||
        (port->receive == NULL) || (port->set_boot0 == NULL) ||
        (port->set_reset == NULL) || (port->delay_ms == NULL) ||
        (config->command_timeout_ms == 0U) || (config->write_timeout_ms == 0U) ||
        (config->erase_timeout_ms == 0U) || (config->reset_settle_ms == 0U))
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    device->port = *port;
    device->config = *config;
    ClearInfo(&device->info);
    device->session = STM32H743_ROM_BOOT_SESSION_CLOSED;
    device->initialized = 1;
    return STM32H743_ROM_BOOT_STATUS_OK;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Enter(stm32h743_rom_boot_t *device)
{
    uint8_t sync = ROM_SYNC;
    stm32h743_rom_boot_status_t status;

    if (device == NULL)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    if ((device->initialized == 0) ||
        (device->session != STM32H743_ROM_BOOT_SESSION_CLOSED))
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    ClearInfo(&device->info);
    status = device->port.configure_uart(device->port.context, STM32H743_ROM_BOOT_UART_ROM_8E1);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    status = device->port.set_boot0(device->port.context, 1);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        (void)RestoreApplication(device);
        return status;
    }
    status = device->port.set_reset(device->port.context, 1);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        (void)RestoreApplication(device);
        return status;
    }
    device->port.delay_ms(device->port.context, device->config.reset_settle_ms);
    status = device->port.set_reset(device->port.context, 0);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        (void)RestoreApplication(device);
        return status;
    }
    device->port.delay_ms(device->port.context, device->config.reset_settle_ms);
    status = Tx(device, &sync, 1U, device->config.command_timeout_ms);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        (void)RestoreApplication(device);
        return status;
    }
    device->session = STM32H743_ROM_BOOT_SESSION_SYNCED;
    return STM32H743_ROM_BOOT_STATUS_OK;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_GetInfo(
    stm32h743_rom_boot_t *device, stm32h743_rom_boot_info_t *info)
{
    stm32h743_rom_boot_info_t parsed;
    uint8_t count_minus_one;
    uint8_t id[2];
    uint32_t response_count;
    stm32h743_rom_boot_status_t status;

    if (info == NULL)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    status = RequireSession(device, 0);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    ClearInfo(&parsed);
    status = SendCommand(device, ROM_CMD_GET);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }
    status = Rx(device, &count_minus_one, 1U, device->config.command_timeout_ms);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }
    response_count = (uint32_t)count_minus_one + 1U;
    if (response_count > (STM32H743_ROM_BOOT_MAX_COMMAND_COUNT + 1U))
    {
        return Fault(device, STM32H743_ROM_BOOT_STATUS_PROTOCOL_ERROR);
    }
    parsed.command_count = (uint8_t)(response_count - 1U);
    status = Rx(device, &parsed.bootloader_version, 1U, device->config.command_timeout_ms);
    if ((status == STM32H743_ROM_BOOT_STATUS_OK) && (parsed.command_count != 0U))
    {
        status = Rx(device, parsed.commands, parsed.command_count,
                    device->config.command_timeout_ms);
    }
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }

    device->info = parsed;
    if (!HasCommand(device, ROM_CMD_GET_ID) || !HasCommand(device, ROM_CMD_READ_MEMORY) ||
        !HasCommand(device, ROM_CMD_WRITE_MEMORY) || !HasCommand(device, ROM_CMD_EXTENDED_ERASE))
    {
        return Fault(device, STM32H743_ROM_BOOT_STATUS_NOT_SUPPORTED);
    }
    status = SendCommand(device, ROM_CMD_GET_ID);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = Rx(device, &count_minus_one, 1U, device->config.command_timeout_ms);
    }
    if ((status != STM32H743_ROM_BOOT_STATUS_OK) || (count_minus_one != 1U))
    {
        return Fault(device, (status != STM32H743_ROM_BOOT_STATUS_OK)
                                 ? status
                                 : STM32H743_ROM_BOOT_STATUS_PROTOCOL_ERROR);
    }
    status = Rx(device, id, sizeof(id), device->config.command_timeout_ms);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }
    parsed.device_id = (uint16_t)(((uint16_t)id[0] << 8U) | id[1]);
    device->info = parsed;
    *info = parsed;
    if (parsed.device_id != STM32H743_ROM_BOOT_DEVICE_ID)
    {
        return STM32H743_ROM_BOOT_STATUS_WRONG_DEVICE;
    }
    device->session = STM32H743_ROM_BOOT_SESSION_READY;
    return STM32H743_ROM_BOOT_STATUS_OK;
}

static stm32h743_rom_boot_status_t ReadBlock(stm32h743_rom_boot_t *device, uint32_t address,
                                             uint8_t *data, uint32_t size)
{
    uint8_t length[2] = {(uint8_t)(size - 1U), (uint8_t)~(uint8_t)(size - 1U)};
    stm32h743_rom_boot_status_t status = SendCommand(device, ROM_CMD_READ_MEMORY);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = SendAddress(device, address);
    }
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = Tx(device, length, sizeof(length), device->config.command_timeout_ms);
    }
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = Rx(device, data, size, device->config.command_timeout_ms);
    }
    return status;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Read(
    stm32h743_rom_boot_t *device, uint32_t address, void *data, uint32_t size)
{
    uint8_t *output = (uint8_t *)data;
    uint32_t remaining = size;
    stm32h743_rom_boot_status_t status;

    if (data == NULL)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    status = RequireSession(device, 1);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(address, size);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    while (remaining != 0U)
    {
        uint32_t chunk = (remaining > STM32H743_ROM_BOOT_MAX_TRANSFER)
                             ? STM32H743_ROM_BOOT_MAX_TRANSFER
                             : remaining;
        status = ReadBlock(device, address, output, chunk);
        if (status != STM32H743_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }
        address += chunk;
        output += chunk;
        remaining -= chunk;
    }
    return STM32H743_ROM_BOOT_STATUS_OK;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Erase(
    stm32h743_rom_boot_t *device, uint32_t address, uint32_t size)
{
    uint8_t frame[ROM_ERASE_FRAME_SIZE];
    uint32_t first;
    uint32_t last;
    uint32_t count;
    uint32_t i;
    uint32_t payload_size;
    stm32h743_rom_boot_status_t status = RequireSession(device, 1);

    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(address, size);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    first = (address - STM32H743_ROM_BOOT_FLASH_BASE) / STM32H743_ROM_BOOT_SECTOR_SIZE;
    last = (address + size - 1U - STM32H743_ROM_BOOT_FLASH_BASE) /
           STM32H743_ROM_BOOT_SECTOR_SIZE;
    count = last - first + 1U;
    if ((device->info.bootloader_version == ROM_VERSION_V91) &&
        (last >= STM32H743_ROM_BOOT_BANK2_FIRST_SECTOR) &&
        (device->config.v91_bank2_erase_guard_ms == 0U))
    {
        return STM32H743_ROM_BOOT_STATUS_NOT_SUPPORTED;
    }
    status = SendCommand(device, ROM_CMD_EXTENDED_ERASE);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }
    frame[0] = (uint8_t)((count - 1U) >> 8U);
    frame[1] = (uint8_t)(count - 1U);
    for (i = 0U; i < count; ++i)
    {
        uint16_t sector = (uint16_t)(first + i);
        frame[2U + i * 2U] = (uint8_t)(sector >> 8U);
        frame[3U + i * 2U] = (uint8_t)sector;
    }
    payload_size = 2U + count * 2U;
    frame[payload_size] = XorBytes(frame, payload_size);
    status = Tx(device, frame, payload_size + 1U, device->config.command_timeout_ms);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.erase_timeout_ms);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }
    if ((device->info.bootloader_version == ROM_VERSION_V91) &&
        (last >= STM32H743_ROM_BOOT_BANK2_FIRST_SECTOR))
    {
        device->port.delay_ms(device->port.context,
                              device->config.v91_bank2_erase_guard_ms);
    }
    return STM32H743_ROM_BOOT_STATUS_OK;
}

static stm32h743_rom_boot_status_t WriteBlock(stm32h743_rom_boot_t *device, uint32_t address,
                                              const uint8_t *data, uint32_t size)
{
    uint8_t frame[STM32H743_ROM_BOOT_MAX_TRANSFER + 2U];
    uint32_t i;
    stm32h743_rom_boot_status_t status = SendCommand(device, ROM_CMD_WRITE_MEMORY);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = SendAddress(device, address);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    frame[0] = (uint8_t)(size - 1U);
    for (i = 0U; i < size; ++i)
    {
        frame[i + 1U] = data[i];
    }
    frame[size + 1U] = XorBytes(frame, size + 1U);
    status = Tx(device, frame, size + 2U, device->config.command_timeout_ms);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.write_timeout_ms);
    }
    return status;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Write(
    stm32h743_rom_boot_t *device, uint32_t address, const void *data, uint32_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    uint8_t padded[STM32H743_ROM_BOOT_MAX_TRANSFER];
    uint32_t remaining = size;
    stm32h743_rom_boot_status_t status;

    if ((data == NULL) || ((address % STM32H743_ROM_BOOT_WRITE_ALIGNMENT) != 0U))
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    status = RequireSession(device, 1);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(address, size);
    }
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        uint32_t padded_size = (size + 3U) & ~3U;
        if (padded_size > (STM32H743_ROM_BOOT_FLASH_END_EXCLUSIVE - address))
        {
            status = STM32H743_ROM_BOOT_STATUS_OUT_OF_RANGE;
        }
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    while (remaining != 0U)
    {
        uint32_t actual = (remaining > STM32H743_ROM_BOOT_MAX_TRANSFER)
                              ? STM32H743_ROM_BOOT_MAX_TRANSFER
                              : remaining;
        uint32_t wire_size = (actual + 3U) & ~3U;
        uint32_t i;
        for (i = 0U; i < actual; ++i)
        {
            padded[i] = input[i];
        }
        for (; i < wire_size; ++i)
        {
            padded[i] = 0xFFU;
        }
        status = WriteBlock(device, address, padded, wire_size);
        if (status != STM32H743_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }
        address += wire_size;
        input += actual;
        remaining -= actual;
    }
    return STM32H743_ROM_BOOT_STATUS_OK;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Verify(
    stm32h743_rom_boot_t *device, uint32_t address, const void *data, uint32_t size)
{
    const uint8_t *expected = (const uint8_t *)data;
    uint8_t readback[STM32H743_ROM_BOOT_MAX_TRANSFER];
    uint32_t remaining = size;
    stm32h743_rom_boot_status_t status;

    if (data == NULL)
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }
    status = RequireSession(device, 1);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(address, size);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return status;
    }
    while (remaining != 0U)
    {
        uint32_t chunk = (remaining > sizeof(readback)) ? sizeof(readback) : remaining;
        uint32_t i;
        status = ReadBlock(device, address, readback, chunk);
        if (status != STM32H743_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }
        for (i = 0U; i < chunk; ++i)
        {
            if (readback[i] != expected[i])
            {
                return STM32H743_ROM_BOOT_STATUS_VERIFY_FAILED;
            }
        }
        address += chunk;
        expected += chunk;
        remaining -= chunk;
    }
    return STM32H743_ROM_BOOT_STATUS_OK;
}

stm32h743_rom_boot_status_t Stm32H743RomBoot_Leave(stm32h743_rom_boot_t *device)
{
    if ((device == NULL) || (device->initialized == 0) ||
        (device->session == STM32H743_ROM_BOOT_SESSION_CLOSED))
    {
        return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
    }
    return RestoreApplication(device);
}
