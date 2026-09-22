#include "stm32_rom_boot.h"

#include <stddef.h>

#define ROM_SYNC 0x7FU
#define ROM_ACK  0x79U
#define ROM_NACK 0x1FU

#define ROM_CMD_GET            0x00U
#define ROM_CMD_GET_ID         0x02U
#define ROM_CMD_READ_MEMORY    0x11U
#define ROM_CMD_WRITE_MEMORY   0x31U
#define ROM_CMD_ERASE          0x43U
#define ROM_CMD_EXTENDED_ERASE 0x44U

#define ROM_VERSION_V91 0x91U

#define ROM_STANDARD_ERASE_FRAME_SIZE (1U + STM32_ROM_BOOT_MAX_ERASE_UNITS + 1U)

#define ROM_EXTENDED_ERASE_FRAME_SIZE (2U + (STM32_ROM_BOOT_MAX_ERASE_UNITS * 2U) + 1U)


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
    data[0] = (uint8_t) (value >> 24U);
    data[1] = (uint8_t) (value >> 16U);
    data[2] = (uint8_t) (value >> 8U);
    data[3] = (uint8_t) value;
}


static uint32_t RoundUp(uint32_t value, uint32_t alignment)
{
    uint32_t remainder = value % alignment;

    return (remainder == 0U) ? value : (value + alignment - remainder);
}


static void ClearInfo(stm32_rom_boot_info_t *info)
{
    uint32_t i;

    info->bootloader_version = 0U;
    info->device_id          = 0xFFFFU;
    info->command_count      = 0U;

    for (i = 0U; i < STM32_ROM_BOOT_MAX_COMMAND_COUNT; ++i)
    {
        info->commands[i] = 0U;
    }
}


static int HasCommand(const stm32_rom_boot_t *device, uint8_t command)
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


static stm32_rom_boot_status_t Fault(stm32_rom_boot_t *device, stm32_rom_boot_status_t status)
{
    if (device != NULL)
    {
        device->session = STM32_ROM_BOOT_SESSION_FAULTED;
    }

    return status;
}


static stm32_rom_boot_status_t Tx(stm32_rom_boot_t *device, const uint8_t *data, uint32_t size,
                                  uint32_t timeout_ms)
{
    return device->port.transmit(device->port.context, data, size, timeout_ms);
}


static stm32_rom_boot_status_t Rx(stm32_rom_boot_t *device, uint8_t *data, uint32_t size,
                                  uint32_t timeout_ms)
{
    return device->port.receive(device->port.context, data, size, timeout_ms);
}


static stm32_rom_boot_status_t ReceiveAck(stm32_rom_boot_t *device, uint32_t timeout_ms)
{
    uint8_t response;

    stm32_rom_boot_status_t status = Rx(device, &response, 1U, timeout_ms);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    if (response == ROM_NACK)
    {
        return STM32_ROM_BOOT_STATUS_NACK;
    }

    return (response == ROM_ACK) ? STM32_ROM_BOOT_STATUS_OK : STM32_ROM_BOOT_STATUS_PROTOCOL_ERROR;
}


static stm32_rom_boot_status_t SendCommand(stm32_rom_boot_t *device, uint8_t command)
{
    uint8_t frame[2] = {command, (uint8_t) ~command};

    stm32_rom_boot_status_t status =
        Tx(device, frame, sizeof(frame), device->config.command_timeout_ms);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    return ReceiveAck(device, device->config.command_timeout_ms);
}


static stm32_rom_boot_status_t SendAddress(stm32_rom_boot_t *device, uint32_t address)
{
    uint8_t frame[5];

    stm32_rom_boot_status_t status;

    PutBe32(frame, address);

    frame[4] = XorBytes(frame, 4U);

    status = Tx(device, frame, sizeof(frame), device->config.command_timeout_ms);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    return ReceiveAck(device, device->config.command_timeout_ms);
}


static stm32_rom_boot_status_t RequireSession(const stm32_rom_boot_t *device, int ready)
{
    if (device == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    if ((device->initialized == 0) || (device->profile == NULL))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    if (device->session == STM32_ROM_BOOT_SESSION_FAULTED)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    if (ready != 0)
    {
        return (device->session == STM32_ROM_BOOT_SESSION_READY)
                   ? STM32_ROM_BOOT_STATUS_OK
                   : STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    return (device->session == STM32_ROM_BOOT_SESSION_SYNCED) ? STM32_ROM_BOOT_STATUS_OK
                                                              : STM32_ROM_BOOT_STATUS_INVALID_STATE;
}


static stm32_rom_boot_status_t ValidateRange(const stm32_rom_boot_t *device, uint32_t address,
                                             uint32_t size)
{
    uint32_t flash_end;

    if ((device == NULL) || (device->profile == NULL) || (size == 0U))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    flash_end = device->profile->flash_base + device->profile->flash_size;

    if ((address < device->profile->flash_base) || (address >= flash_end) ||
        (size > (flash_end - address)))
    {
        return STM32_ROM_BOOT_STATUS_OUT_OF_RANGE;
    }

    return STM32_ROM_BOOT_STATUS_OK;
}


static stm32_rom_boot_status_t RestoreApplication(stm32_rom_boot_t *device)
{
    stm32_rom_boot_status_t first = STM32_ROM_BOOT_STATUS_OK;

    stm32_rom_boot_status_t status;

    /*
     * BOOT0 low -> next reset boots application Flash.
     */
    status = device->port.set_boot0(device->port.context, 0);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        first = status;
    }

    /*
     * Restore UART configuration used by the normal application.
     */
    status = device->port.configure_uart(device->port.context, STM32_ROM_BOOT_UART_APPLICATION);

    if ((status != STM32_ROM_BOOT_STATUS_OK) && (first == STM32_ROM_BOOT_STATUS_OK))
    {
        first = status;
    }

    /*
     * Assert reset.
     */
    status = device->port.set_reset(device->port.context, 1);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        device->port.delay_ms(device->port.context, device->config.reset_settle_ms);

        /*
         * Release reset.
         */
        status = device->port.set_reset(device->port.context, 0);
    }

    if ((status != STM32_ROM_BOOT_STATUS_OK) && (first == STM32_ROM_BOOT_STATUS_OK))
    {
        first = status;
    }

    device->session = STM32_ROM_BOOT_SESSION_CLOSED;

    return first;
}


stm32_rom_boot_status_t Stm32RomBoot_Init(stm32_rom_boot_t *device,
                                          const stm32_rom_boot_port_t *port,
                                          const stm32_rom_boot_config_t *config)
{
    const stm32_rom_boot_device_profile_t *profile;

    if ((device == NULL) || (port == NULL) || (config == NULL) || (port->configure_uart == NULL) ||
        (port->transmit == NULL) || (port->receive == NULL) || (port->set_boot0 == NULL) ||
        (port->set_reset == NULL) || (port->delay_ms == NULL) ||
        (config->command_timeout_ms == 0U) || (config->write_timeout_ms == 0U) ||
        (config->erase_timeout_ms == 0U) || (config->reset_settle_ms == 0U))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    if (device->initialized != 0)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    profile = Stm32RomBootDevices_Get(config->target);

    if ((profile == NULL) || (profile->flash_size == 0U) || (profile->erase_unit_size == 0U) ||
        (profile->erase_unit_count == 0U) ||
        (profile->erase_unit_count > STM32_ROM_BOOT_MAX_ERASE_UNITS) ||
        (profile->write_alignment == 0U))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    device->port        = *port;
    device->config      = *config;
    device->profile     = profile;
    device->session     = STM32_ROM_BOOT_SESSION_CLOSED;
    device->initialized = 1;

    ClearInfo(&device->info);

    return STM32_ROM_BOOT_STATUS_OK;
}


stm32_rom_boot_status_t Stm32RomBoot_Enter(stm32_rom_boot_t *device)
{
    uint8_t sync = ROM_SYNC;

    stm32_rom_boot_status_t status;

    if (device == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    if ((device->initialized == 0) || (device->profile == NULL) ||
        (device->session != STM32_ROM_BOOT_SESSION_CLOSED))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    ClearInfo(&device->info);

    /*
     * STM32 ROM USART bootloader:
     *
     * 8 data bits
     * even parity
     * 1 stop bit
     */
    status = device->port.configure_uart(device->port.context, STM32_ROM_BOOT_UART_ROM_8E1);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * BOOT0 high -> System Memory after reset.
     */
    status = device->port.set_boot0(device->port.context, 1);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        (void) RestoreApplication(device);
        return status;
    }

    /*
     * Assert target reset.
     */
    status = device->port.set_reset(device->port.context, 1);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        (void) RestoreApplication(device);
        return status;
    }

    device->port.delay_ms(device->port.context, device->config.reset_settle_ms);

    /*
     * Release reset.
     */
    status = device->port.set_reset(device->port.context, 0);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        (void) RestoreApplication(device);
        return status;
    }

    device->port.delay_ms(device->port.context, device->config.reset_settle_ms);

    /*
     * AN3155 synchronization byte.
     */
    status = Tx(device, &sync, 1U, device->config.command_timeout_ms);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        (void) RestoreApplication(device);
        return status;
    }

    device->session = STM32_ROM_BOOT_SESSION_SYNCED;

    return STM32_ROM_BOOT_STATUS_OK;
}


stm32_rom_boot_status_t Stm32RomBoot_GetInfo(stm32_rom_boot_t *device, stm32_rom_boot_info_t *info)
{
    stm32_rom_boot_info_t parsed;

    uint8_t count_minus_one;
    uint8_t id[2];

    uint32_t response_count;

    uint8_t required_erase_command;

    stm32_rom_boot_status_t status;

    if (info == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = RequireSession(device, 0);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    ClearInfo(&parsed);

    /*
     * GET
     */
    status = SendCommand(device, ROM_CMD_GET);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }

    /*
     * N: number of following bytes minus one.
     */
    status = Rx(device, &count_minus_one, 1U, device->config.command_timeout_ms);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }

    response_count = (uint32_t) count_minus_one + 1U;

    if ((response_count == 0U) || (response_count > (STM32_ROM_BOOT_MAX_COMMAND_COUNT + 1U)))
    {
        return Fault(device, STM32_ROM_BOOT_STATUS_PROTOCOL_ERROR);
    }

    /*
     * response:
     *
     * version
     * command 0
     * command 1
     * ...
     */
    parsed.command_count = (uint8_t) (response_count - 1U);

    status = Rx(device, &parsed.bootloader_version, 1U, device->config.command_timeout_ms);

    if ((status == STM32_ROM_BOOT_STATUS_OK) && (parsed.command_count != 0U))
    {
        status =
            Rx(device, parsed.commands, parsed.command_count, device->config.command_timeout_ms);
    }

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }

    /*
     * HasCommand() works on device->info.
     */
    device->info = parsed;

    /*
     * Erase command is device-dependent.
     *
     * F103C8:
     *     0x43 Erase
     *
     * H743:
     *     0x44 Extended Erase
     */
    required_erase_command = (device->profile->erase_method == STM32_ROM_BOOT_ERASE_STANDARD)
                                 ? ROM_CMD_ERASE
                                 : ROM_CMD_EXTENDED_ERASE;

    if (!HasCommand(device, ROM_CMD_GET_ID) || !HasCommand(device, ROM_CMD_READ_MEMORY) ||
        !HasCommand(device, ROM_CMD_WRITE_MEMORY) || !HasCommand(device, required_erase_command))
    {
        *info = parsed;

        return Fault(device, STM32_ROM_BOOT_STATUS_NOT_SUPPORTED);
    }

    /*
     * GET ID
     */
    status = SendCommand(device, ROM_CMD_GET_ID);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = Rx(device, &count_minus_one, 1U, device->config.command_timeout_ms);
    }

    /*
     * STM32 Device ID is two bytes,
     * therefore N must be 1.
     */
    if ((status != STM32_ROM_BOOT_STATUS_OK) || (count_minus_one != 1U))
    {
        *info = parsed;

        return Fault(device, (status != STM32_ROM_BOOT_STATUS_OK)
                                 ? status
                                 : STM32_ROM_BOOT_STATUS_PROTOCOL_ERROR);
    }

    status = Rx(device, id, sizeof(id), device->config.command_timeout_ms);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        *info = parsed;
        return Fault(device, status);
    }

    parsed.device_id = (uint16_t) (((uint16_t) id[0] << 8U) | id[1]);

    device->info = parsed;
    *info        = parsed;

    /*
     * Validate selected target against the actual
     * ROM Device ID.
     *
     * Important:
     *
     * STM32F103C8 and F103CB are both medium-density
     * family parts, so Device ID alone must not be
     * used to infer the exact Flash capacity.
     *
     * config.target provides the exact expected target.
     */
    if (parsed.device_id != device->profile->device_id)
    {
        return Fault(device, STM32_ROM_BOOT_STATUS_WRONG_DEVICE);
    }

    device->session = STM32_ROM_BOOT_SESSION_READY;

    return STM32_ROM_BOOT_STATUS_OK;
}


static stm32_rom_boot_status_t ReadBlock(stm32_rom_boot_t *device, uint32_t address, uint8_t *data,
                                         uint32_t size)
{
    uint8_t length[2] = {(uint8_t) (size - 1U), (uint8_t) ~(uint8_t) (size - 1U)};

    stm32_rom_boot_status_t status = SendCommand(device, ROM_CMD_READ_MEMORY);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = SendAddress(device, address);
    }

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = Tx(device, length, sizeof(length), device->config.command_timeout_ms);
    }

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.command_timeout_ms);
    }

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = Rx(device, data, size, device->config.command_timeout_ms);
    }

    return status;
}


stm32_rom_boot_status_t Stm32RomBoot_Read(stm32_rom_boot_t *device, uint32_t address, void *data,
                                          uint32_t size)
{
    uint8_t *output = (uint8_t *) data;

    uint32_t remaining = size;

    stm32_rom_boot_status_t status;

    if (data == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = RequireSession(device, 1);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(device, address, size);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    while (remaining != 0U)
    {
        uint32_t chunk =
            (remaining > STM32_ROM_BOOT_MAX_TRANSFER) ? STM32_ROM_BOOT_MAX_TRANSFER : remaining;

        status = ReadBlock(device, address, output, chunk);

        if (status != STM32_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }

        address += chunk;
        output += chunk;
        remaining -= chunk;
    }

    return STM32_ROM_BOOT_STATUS_OK;
}


static stm32_rom_boot_status_t EraseStandard(stm32_rom_boot_t *device, uint32_t first,
                                             uint32_t count)
{
    uint8_t frame[ROM_STANDARD_ERASE_FRAME_SIZE];

    uint32_t i;
    uint32_t payload_size;

    stm32_rom_boot_status_t status;

    /*
     * Standard Erase uses one-byte page numbers.
     */
    if ((count == 0U) || (count > 256U) || ((first + count) > 256U))
    {
        return STM32_ROM_BOOT_STATUS_NOT_SUPPORTED;
    }

    status = SendCommand(device, ROM_CMD_ERASE);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * Byte 0:
     *     number of pages - 1
     *
     * Byte 1..N:
     *     page numbers
     *
     * Final byte:
     *     XOR
     */
    frame[0] = (uint8_t) (count - 1U);

    for (i = 0U; i < count; ++i)
    {
        frame[1U + i] = (uint8_t) (first + i);
    }

    payload_size = 1U + count;

    frame[payload_size] = XorBytes(frame, payload_size);

    status = Tx(device, frame, payload_size + 1U, device->config.command_timeout_ms);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.erase_timeout_ms);
    }

    return status;
}


static stm32_rom_boot_status_t EraseExtended(stm32_rom_boot_t *device, uint32_t first,
                                             uint32_t count)
{
    uint8_t frame[ROM_EXTENDED_ERASE_FRAME_SIZE];

    uint32_t i;
    uint32_t payload_size;

    stm32_rom_boot_status_t status;

    if ((count == 0U) || (count > STM32_ROM_BOOT_MAX_ERASE_UNITS) ||
        ((first + count - 1U) > 0xFFFFU))
    {
        return STM32_ROM_BOOT_STATUS_NOT_SUPPORTED;
    }

    status = SendCommand(device, ROM_CMD_EXTENDED_ERASE);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * Extended Erase:
     *
     * bytes 0..1:
     *     N = count - 1, big endian
     */
    frame[0] = (uint8_t) ((count - 1U) >> 8U);

    frame[1] = (uint8_t) (count - 1U);

    /*
     * Each erase unit is encoded using two bytes,
     * MSB first.
     */
    for (i = 0U; i < count; ++i)
    {
        uint16_t unit = (uint16_t) (first + i);

        frame[2U + i * 2U] = (uint8_t) (unit >> 8U);

        frame[3U + i * 2U] = (uint8_t) unit;
    }

    payload_size = 2U + count * 2U;

    frame[payload_size] = XorBytes(frame, payload_size);

    status = Tx(device, frame, payload_size + 1U, device->config.command_timeout_ms);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.erase_timeout_ms);
    }

    return status;
}


stm32_rom_boot_status_t Stm32RomBoot_Erase(stm32_rom_boot_t *device, uint32_t address,
                                           uint32_t size)
{
    uint32_t first;
    uint32_t last;
    uint32_t count;

    int h743_v91_bank2_guard = 0;

    stm32_rom_boot_status_t status = RequireSession(device, 1);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(device, address, size);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * Map byte address range onto the target-specific
     * erase-unit geometry.
     *
     * H743:
     *     128 KiB sector
     *
     * F103C8:
     *     1 KiB page
     */
    first = (address - device->profile->flash_base) / device->profile->erase_unit_size;

    last = (address + size - 1U - device->profile->flash_base) / device->profile->erase_unit_size;

    count = last - first + 1U;

    if ((last >= device->profile->erase_unit_count) || (count > STM32_ROM_BOOT_MAX_ERASE_UNITS))
    {
        return STM32_ROM_BOOT_STATUS_OUT_OF_RANGE;
    }

    /*
     * Preserve the original STM32H743 ROM V9.1
     * Bank-2 workaround.
     *
     * It does not affect F103.
     */
    if (((device->profile->quirks & STM32_ROM_BOOT_DEVICE_QUIRK_H743_V91_BANK2_ERASE_GUARD) !=
         0U) &&
        (device->info.bootloader_version == ROM_VERSION_V91) &&
        (device->profile->bank2_first_erase_unit != STM32_ROM_BOOT_NO_BANK2_FIRST_UNIT) &&
        (last >= device->profile->bank2_first_erase_unit))
    {
        h743_v91_bank2_guard = 1;

        if (device->config.v91_bank2_erase_guard_ms == 0U)
        {
            return STM32_ROM_BOOT_STATUS_NOT_SUPPORTED;
        }
    }

    switch (device->profile->erase_method)
    {
        case STM32_ROM_BOOT_ERASE_STANDARD:

            status = EraseStandard(device, first, count);

            break;

        case STM32_ROM_BOOT_ERASE_EXTENDED:

            status = EraseExtended(device, first, count);

            break;

        default:

            return STM32_ROM_BOOT_STATUS_NOT_SUPPORTED;
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return Fault(device, status);
    }

    if (h743_v91_bank2_guard != 0)
    {
        device->port.delay_ms(device->port.context, device->config.v91_bank2_erase_guard_ms);
    }

    return STM32_ROM_BOOT_STATUS_OK;
}


static stm32_rom_boot_status_t WriteBlock(stm32_rom_boot_t *device, uint32_t address,
                                          const uint8_t *data, uint32_t size)
{
    uint8_t frame[STM32_ROM_BOOT_MAX_TRANSFER + 2U];

    uint32_t i;

    stm32_rom_boot_status_t status;

    /*
     * AN3155 Write Memory payload must contain
     * 1..256 bytes and N+1 must be a multiple of 4.
     */
    if ((size == 0U) || (size > STM32_ROM_BOOT_MAX_TRANSFER) || ((size % 4U) != 0U))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = SendCommand(device, ROM_CMD_WRITE_MEMORY);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = SendAddress(device, address);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * N = number of bytes - 1.
     */
    frame[0] = (uint8_t) (size - 1U);

    for (i = 0U; i < size; ++i)
    {
        frame[i + 1U] = data[i];
    }

    /*
     * XOR includes N and all data bytes.
     */
    frame[size + 1U] = XorBytes(frame, size + 1U);

    status = Tx(device, frame, size + 2U, device->config.command_timeout_ms);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ReceiveAck(device, device->config.write_timeout_ms);
    }

    return status;
}


stm32_rom_boot_status_t Stm32RomBoot_Write(stm32_rom_boot_t *device, uint32_t address,
                                           const void *data, uint32_t size)
{
    const uint8_t *input = (const uint8_t *) data;

    uint8_t padded[STM32_ROM_BOOT_MAX_TRANSFER];

    uint32_t remaining = size;

    uint32_t alignment;
    uint32_t padded_size;
    uint32_t flash_end;

    stm32_rom_boot_status_t status;

    if (data == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = RequireSession(device, 1);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    alignment = device->profile->write_alignment;

    if ((alignment == 0U) || ((address % alignment) != 0U))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = ValidateRange(device, address, size);

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    /*
     * Account for final 0xFF padding.
     */
    padded_size = RoundUp(size, alignment);

    flash_end = device->profile->flash_base + device->profile->flash_size;

    if ((padded_size < size) || (padded_size > (flash_end - address)))
    {
        return STM32_ROM_BOOT_STATUS_OUT_OF_RANGE;
    }

    while (remaining != 0U)
    {
        uint32_t actual =
            (remaining > STM32_ROM_BOOT_MAX_TRANSFER) ? STM32_ROM_BOOT_MAX_TRANSFER : remaining;

        uint32_t wire_size = RoundUp(actual, alignment);

        uint32_t i;

        /*
         * AN3155 permits at most 256 bytes per
         * Write Memory operation.
         */
        if ((wire_size > STM32_ROM_BOOT_MAX_TRANSFER) || ((wire_size % 4U) != 0U))
        {
            return STM32_ROM_BOOT_STATUS_NOT_SUPPORTED;
        }

        for (i = 0U; i < actual; ++i)
        {
            padded[i] = input[i];
        }

        /*
         * Pad final packet to protocol alignment.
         */
        for (; i < wire_size; ++i)
        {
            padded[i] = 0xFFU;
        }

        status = WriteBlock(device, address, padded, wire_size);

        if (status != STM32_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }

        address += wire_size;
        input += actual;
        remaining -= actual;
    }

    return STM32_ROM_BOOT_STATUS_OK;
}


stm32_rom_boot_status_t Stm32RomBoot_Verify(stm32_rom_boot_t *device, uint32_t address,
                                            const void *data, uint32_t size)
{
    const uint8_t *expected = (const uint8_t *) data;

    uint8_t readback[STM32_ROM_BOOT_MAX_TRANSFER];

    uint32_t remaining = size;

    stm32_rom_boot_status_t status;

    if (data == NULL)
    {
        return STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT;
    }

    status = RequireSession(device, 1);

    if (status == STM32_ROM_BOOT_STATUS_OK)
    {
        status = ValidateRange(device, address, size);
    }

    if (status != STM32_ROM_BOOT_STATUS_OK)
    {
        return status;
    }

    while (remaining != 0U)
    {
        uint32_t chunk = (remaining > sizeof(readback)) ? sizeof(readback) : remaining;

        uint32_t i;

        status = ReadBlock(device, address, readback, chunk);

        if (status != STM32_ROM_BOOT_STATUS_OK)
        {
            return Fault(device, status);
        }

        for (i = 0U; i < chunk; ++i)
        {
            if (readback[i] != expected[i])
            {
                return STM32_ROM_BOOT_STATUS_VERIFY_FAILED;
            }
        }

        address += chunk;
        expected += chunk;
        remaining -= chunk;
    }

    return STM32_ROM_BOOT_STATUS_OK;
}


stm32_rom_boot_status_t Stm32RomBoot_Leave(stm32_rom_boot_t *device)
{
    if ((device == NULL) || (device->initialized == 0) ||
        (device->session == STM32_ROM_BOOT_SESSION_CLOSED))
    {
        return STM32_ROM_BOOT_STATUS_INVALID_STATE;
    }

    return RestoreApplication(device);
}