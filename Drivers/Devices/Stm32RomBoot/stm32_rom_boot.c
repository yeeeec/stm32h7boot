/**
 * @file stm32_rom_boot.c
 * @brief STM32 System Memory Bootloader 的 AN3155 UART 协议实现。
 */
#include "stm32_rom_boot.h"

#include <stddef.h>

#define STM32_ROM_BOOT_UNSPECIFIED_DEVICE_ID 0xFFFFU
#define STM32_ROM_BOOT_STANDARD_MAX_PAGE_COUNT 255U

static uint8_t XorBytes(const uint8_t *data, uint32_t size)
{
    uint8_t checksum = 0U;
    uint32_t index;

    for (index = 0U; index < size; ++index)
    {
        checksum ^= data[index];
    }
    return checksum;
}

static void PutBigEndian32(uint8_t data[4], uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static firmware_status_t Transmit(
    stm32_rom_boot_t *device,
    const uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    return device->port.transmit(
        device->port.context, data, size, timeout_ms);
}

static firmware_status_t Receive(
    stm32_rom_boot_t *device,
    uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    return device->port.receive(
        device->port.context, data, size, timeout_ms);
}

static firmware_status_t ReceiveAck(
    stm32_rom_boot_t *device,
    uint32_t timeout_ms)
{
    uint8_t response;
    firmware_status_t status = Receive(device, &response, 1U, timeout_ms);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (response == STM32_ROM_BOOT_NACK_BYTE)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    return (response == STM32_ROM_BOOT_ACK_BYTE)
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t SendCommand(
    stm32_rom_boot_t *device,
    uint8_t command)
{
    uint8_t frame[2] = {command, (uint8_t)~command};
    firmware_status_t status = Transmit(
        device, frame, sizeof(frame), device->config.command_timeout_ms);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return ReceiveAck(device, device->config.command_timeout_ms);
}

static firmware_status_t SendAddress(
    stm32_rom_boot_t *device,
    uint32_t address)
{
    uint8_t frame[5];

    PutBigEndian32(frame, address);
    frame[4] = XorBytes(frame, 4U);
    return Transmit(
        device, frame, sizeof(frame), device->config.command_timeout_ms);
}

static firmware_status_t ValidateInitialized(
    const stm32_rom_boot_t *device)
{
    if (device == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return (device->initialized != 0)
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t ValidateInBootloader(
    const stm32_rom_boot_t *device)
{
    firmware_status_t status = ValidateInitialized(device);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return (device->in_bootloader != 0)
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static int InfoContainsCommand(
    const stm32_rom_boot_info_t *info,
    uint8_t command)
{
    uint32_t index;

    for (index = 0U; index < info->command_count; ++index)
    {
        if (info->commands[index] == command)
        {
            return 1;
        }
    }
    return 0;
}

static void ClearInfo(stm32_rom_boot_info_t *info)
{
    uint32_t index;

    info->bootloader_version = 0U;
    info->device_id = STM32_ROM_BOOT_UNSPECIFIED_DEVICE_ID;
    info->command_count = 0U;
    for (index = 0U; index < STM32_ROM_BOOT_MAX_COMMAND_COUNT; ++index)
    {
        info->commands[index] = 0U;
    }
}

static firmware_status_t RestoreApplication(
    stm32_rom_boot_t *device)
{
    firmware_status_t status;
    firmware_status_t first_error = FIRMWARE_STATUS_OK;

    /* 即使前一个步骤失败，也继续执行拉低 BOOT0，避免目标停在 ROM 中。 */
    status = device->port.set_boot0(device->port.context, 0);
    if (!FirmwareStatus_IsOk(status))
    {
        first_error = status;
    }

    if (device->port.configure_uart != NULL)
    {
        status = device->port.configure_uart(
            device->port.context, STM32_ROM_BOOT_UART_APPLICATION_MODE);
        if (!FirmwareStatus_IsOk(status) && FirmwareStatus_IsOk(first_error))
        {
            first_error = status;
        }
    }

    status = device->port.reset_target(device->port.context);
    if (!FirmwareStatus_IsOk(status) && FirmwareStatus_IsOk(first_error))
    {
        first_error = status;
    }

    device->in_bootloader = 0;
    return first_error;
}

firmware_status_t Stm32RomBoot_Init(
    stm32_rom_boot_t *device,
    const stm32_rom_boot_port_t *port,
    const stm32_rom_boot_config_t *config)
{
    if ((device == NULL) || (port == NULL) || (config == NULL) ||
        (port->transmit == NULL) || (port->receive == NULL) ||
        (port->set_boot0 == NULL) || (port->reset_target == NULL) ||
        (port->delay_ms == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((config->command_timeout_ms == 0U) ||
        (config->write_timeout_ms == 0U) ||
        (config->erase_timeout_ms == 0U) ||
        (config->reset_settle_ms == 0U) ||
        (config->erase_mode != STM32_ROM_BOOT_ERASE_STANDARD &&
         config->erase_mode != STM32_ROM_BOOT_ERASE_EXTENDED))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    device->port = *port;
    device->config = *config;
    ClearInfo(&device->info);
    device->initialized = 1;
    device->in_bootloader = 0;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_Enter(stm32_rom_boot_t *device)
{
    firmware_status_t status;
    uint8_t sync = STM32_ROM_BOOT_SYNC_BYTE;

    status = ValidateInitialized(device);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (device->in_bootloader != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 每次会话都重新发现目标能力，禁止复用上一次目标留下的命令表。 */
    ClearInfo(&device->info);

    if (device->port.configure_uart != NULL)
    {
        status = device->port.configure_uart(
            device->port.context, STM32_ROM_BOOT_UART_ROM_MODE);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
    }

    status = device->port.set_boot0(device->port.context, 1);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = device->port.reset_target(device->port.context);
    if (!FirmwareStatus_IsOk(status))
    {
        (void)RestoreApplication(device);
        return status;
    }
    device->port.delay_ms(device->port.context, device->config.reset_settle_ms);

    status = Transmit(
        device, &sync, 1U, device->config.command_timeout_ms);
    if (FirmwareStatus_IsOk(status))
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }
    if (!FirmwareStatus_IsOk(status))
    {
        (void)RestoreApplication(device);
        return status;
    }

    device->in_bootloader = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Stm32RomBoot_GetInfo(
    stm32_rom_boot_t *device,
    stm32_rom_boot_info_t *info)
{
    stm32_rom_boot_info_t parsed = {0};
    firmware_status_t status;
    uint8_t count_minus_one;
    uint32_t response_count;
    uint32_t index;

    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = ValidateInBootloader(device);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* Get 是能力发现的入口，不能在命令表尚未读取时反向依赖命令表。 */
    status = SendCommand(device, STM32_ROM_BOOT_COMMAND_GET);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Receive(
        device, &count_minus_one, 1U, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    response_count = (uint32_t)count_minus_one + 1U;
    /* 返回内容至少包含版本号，且不能超过本对象的固定存储空间。 */
    if ((response_count == 0U) ||
        (response_count > (STM32_ROM_BOOT_MAX_COMMAND_COUNT + 1U)))
    {
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }

    parsed.command_count = (uint8_t)(response_count - 1U);
    status = Receive(
        device, &parsed.bootloader_version, 1U,
        device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    for (index = 0U; index < parsed.command_count; ++index)
    {
        status = Receive(
            device, &parsed.commands[index], 1U,
            device->config.command_timeout_ms);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
    }
    status = ReceiveAck(device, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    if (!InfoContainsCommand(&parsed, STM32_ROM_BOOT_COMMAND_GET_ID))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    status = SendCommand(device, STM32_ROM_BOOT_COMMAND_GET_ID);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Receive(
        device, &count_minus_one, 1U, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    response_count = (uint32_t)count_minus_one + 1U;
    if (response_count != 2U)
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    {
        uint8_t id_bytes[2];

        status = Receive(
            device, id_bytes, sizeof(id_bytes),
            device->config.command_timeout_ms);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        parsed.device_id = (uint16_t)(((uint16_t)id_bytes[0] << 8U) | id_bytes[1]);
    }
    status = ReceiveAck(device, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((device->config.expected_device_id != STM32_ROM_BOOT_UNSPECIFIED_DEVICE_ID) &&
        (parsed.device_id != device->config.expected_device_id))
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }

    *info = parsed;
    device->info = parsed;
    return FIRMWARE_STATUS_OK;
}

int Stm32RomBoot_IsCommandSupported(
    const stm32_rom_boot_t *device,
    uint8_t command)
{
    if ((device == NULL) || (device->initialized == 0))
    {
        return 0;
    }
    return InfoContainsCommand(&device->info, command);
}

firmware_status_t Stm32RomBoot_ReadMemory(
    stm32_rom_boot_t *device,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    uint8_t length_frame[2];
    firmware_status_t status = ValidateInBootloader(device);

    if ((data == NULL) || (size == 0U) ||
        (size > STM32_ROM_BOOT_MAX_MEMORY_TRANSFER))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (!Stm32RomBoot_IsCommandSupported(
            device, STM32_ROM_BOOT_COMMAND_READ_MEMORY))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    status = SendCommand(device, STM32_ROM_BOOT_COMMAND_READ_MEMORY);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = SendAddress(device, address);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = ReceiveAck(device, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    length_frame[0] = (uint8_t)(size - 1U);
    length_frame[1] = (uint8_t)~length_frame[0];
    status = Transmit(
        device, length_frame, sizeof(length_frame),
        device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = ReceiveAck(device, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return Receive(
        device, data, size, device->config.command_timeout_ms);
}

firmware_status_t Stm32RomBoot_WriteMemory(
    stm32_rom_boot_t *device,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    uint8_t frame[STM32_ROM_BOOT_MAX_MEMORY_TRANSFER + 2U];
    uint32_t index;
    firmware_status_t status = ValidateInBootloader(device);

    if ((data == NULL) || (size == 0U) ||
        (size > STM32_ROM_BOOT_MAX_MEMORY_TRANSFER))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (!Stm32RomBoot_IsCommandSupported(
            device, STM32_ROM_BOOT_COMMAND_WRITE_MEMORY))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    status = SendCommand(device, STM32_ROM_BOOT_COMMAND_WRITE_MEMORY);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = SendAddress(device, address);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = ReceiveAck(device, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    frame[0] = (uint8_t)(size - 1U);
    for (index = 0U; index < size; ++index)
    {
        frame[index + 1U] = data[index];
    }
    frame[size + 1U] = XorBytes(frame, size + 1U);
    status = Transmit(
        device, frame, size + 2U, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return ReceiveAck(device, device->config.write_timeout_ms);
}

firmware_status_t Stm32RomBoot_ErasePages(
    stm32_rom_boot_t *device,
    uint16_t page_start,
    uint16_t page_count)
{
    uint8_t frame[STM32_ROM_BOOT_MAX_ERASE_FRAME_SIZE];
    uint32_t index;
    uint32_t frame_size;
    firmware_status_t status = ValidateInBootloader(device);

    if ((page_count == 0U) ||
        (page_count > STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (((uint32_t)page_start + page_count) > 65536UL)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    if (device->config.erase_mode == STM32_ROM_BOOT_ERASE_STANDARD)
    {
        /* 标准协议的 N=0xFF 代表特殊全片擦除，因此普通页列表最多 255 页。 */
        if ((page_count > STM32_ROM_BOOT_STANDARD_MAX_PAGE_COUNT) ||
            ((uint32_t)page_start + page_count) > 256U ||
            !Stm32RomBoot_IsCommandSupported(
                device, STM32_ROM_BOOT_COMMAND_ERASE))
        {
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        }
        status = SendCommand(device, STM32_ROM_BOOT_COMMAND_ERASE);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        frame[0] = (uint8_t)(page_count - 1U);
        for (index = 0U; index < page_count; ++index)
        {
            frame[index + 1U] = (uint8_t)(page_start + index);
        }
        frame[page_count + 1U] = XorBytes(frame, page_count + 1U);
        frame_size = (uint32_t)page_count + 2U;
    }
    else
    {
        if (!Stm32RomBoot_IsCommandSupported(
                device, STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE))
        {
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        }
        status = SendCommand(
            device, STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        frame[0] = (uint8_t)((page_count - 1U) >> 8U);
        frame[1] = (uint8_t)(page_count - 1U);
        for (index = 0U; index < page_count; ++index)
        {
            uint32_t offset = 2U + (index * 2U);
            uint16_t page = (uint16_t)(page_start + index);

            frame[offset] = (uint8_t)(page >> 8U);
            frame[offset + 1U] = (uint8_t)page;
        }
        frame[2U + (page_count * 2U)] = XorBytes(
            frame, 2U + (page_count * 2U));
        frame_size = 3U + (uint32_t)page_count * 2U;
    }

    status = Transmit(
        device, frame, frame_size, device->config.command_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* Erase 的最终 ACK 要等目标完成内部擦除，因此使用更长的超时。 */
    return ReceiveAck(device, device->config.erase_timeout_ms);
}

firmware_status_t Stm32RomBoot_Leave(stm32_rom_boot_t *device)
{
    firmware_status_t status;

    status = ValidateInBootloader(device);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = RestoreApplication(device);
    return status;
}
