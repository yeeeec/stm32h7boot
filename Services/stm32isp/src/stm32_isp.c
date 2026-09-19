#include "stm32isp/stm32_isp.h"

#include <string.h>

#define STM32_ISP_SYNC_BYTE 0x7Fu
#define STM32_ISP_ACK       0x79u
#define STM32_ISP_NACK      0x1Fu

#define STM32_ISP_CMD_GET            0x00u
#define STM32_ISP_CMD_GET_ID         0x02u
#define STM32_ISP_CMD_READ_MEMORY    0x11u
#define STM32_ISP_CMD_GO             0x21u
#define STM32_ISP_CMD_WRITE_MEMORY   0x31u
#define STM32_ISP_CMD_ERASE          0x43u
#define STM32_ISP_CMD_EXTENDED_ERASE 0x44u

#define STM32_ISP_RESET_PULSE_MS 20u
#define STM32_ISP_BOOT_WAIT_MS   100u

static stm32_isp_status_t port_result(int result)
{
    if (result == STM32_ISP_PORT_OK)
    {
        return STM32_ISP_OK;
    }
    if (result == STM32_ISP_PORT_TIMEOUT)
    {
        return STM32_ISP_ERROR_TIMEOUT;
    }
    return STM32_ISP_ERROR_IO;
}

static stm32_isp_status_t uart_write(stm32_isp_t *isp, const uint8_t *data, size_t length,
                                     uint32_t timeout_ms)
{
    return port_result(isp->port.uart_write(data, length, timeout_ms, isp->port.user_context));
}

static stm32_isp_status_t uart_read(stm32_isp_t *isp, uint8_t *data, size_t length,
                                    uint32_t timeout_ms)
{
    return port_result(isp->port.uart_read(data, length, timeout_ms, isp->port.user_context));
}

static stm32_isp_status_t wait_ack(stm32_isp_t *isp, uint32_t timeout_ms)
{
    uint8_t response          = 0u;
    stm32_isp_status_t status = uart_read(isp, &response, 1u, timeout_ms);

    if (status != STM32_ISP_OK)
    {
        return status;
    }
    if (response == STM32_ISP_ACK)
    {
        return STM32_ISP_OK;
    }
    if (response == STM32_ISP_NACK)
    {
        return STM32_ISP_ERROR_NACK;
    }
    return STM32_ISP_ERROR_PROTOCOL;
}

static stm32_isp_status_t send_command(stm32_isp_t *isp, uint8_t command)
{
    uint8_t packet[2];
    stm32_isp_status_t status;

    if (!isp->synchronized)
    {
        return STM32_ISP_ERROR_NOT_SYNCHRONIZED;
    }

    packet[0] = command;
    packet[1] = (uint8_t) (command ^ 0xFFu);
    status    = uart_write(isp, packet, sizeof(packet), isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return wait_ack(isp, isp->command_timeout_ms);
}

static stm32_isp_status_t send_address(stm32_isp_t *isp, uint32_t address)
{
    uint8_t packet[5];
    stm32_isp_status_t status;

    packet[0] = (uint8_t) (address >> 24);
    packet[1] = (uint8_t) (address >> 16);
    packet[2] = (uint8_t) (address >> 8);
    packet[3] = (uint8_t) address;
    packet[4] = (uint8_t) (packet[0] ^ packet[1] ^ packet[2] ^ packet[3]);

    status = uart_write(isp, packet, sizeof(packet), isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return wait_ack(isp, isp->command_timeout_ms);
}

static bool command_present(const uint8_t *commands, size_t count, uint8_t command)
{
    size_t i;

    for (i = 0u; i < count; ++i)
    {
        if (commands[i] == command)
        {
            return true;
        }
    }
    return false;
}

stm32_isp_status_t stm32_isp_init(stm32_isp_t *isp, const stm32_isp_port_t *port)
{
    if ((isp == NULL) || (port == NULL) || (port->uart_write == NULL) ||
        (port->uart_read == NULL) || (port->boot0_write == NULL) || (port->nrst_write == NULL) ||
        (port->delay_ms == NULL))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    memset(isp, 0, sizeof(*isp));
    isp->port               = *port;
    isp->command_timeout_ms = STM32_ISP_DEFAULT_TIMEOUT_MS;
    isp->erase_timeout_ms   = STM32_ISP_DEFAULT_ERASE_MS;
    return STM32_ISP_OK;
}

void stm32_isp_set_timeouts(stm32_isp_t *isp, uint32_t command_timeout_ms,
                            uint32_t erase_timeout_ms)
{
    if (isp == NULL)
    {
        return;
    }
    if (command_timeout_ms != 0u)
    {
        isp->command_timeout_ms = command_timeout_ms;
    }
    if (erase_timeout_ms != 0u)
    {
        isp->erase_timeout_ms = erase_timeout_ms;
    }
}

stm32_isp_status_t stm32_isp_enter_bootloader(stm32_isp_t *isp)
{
    if (isp == NULL)
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    isp->synchronized   = false;
    isp->commands_known = false;
    isp->port.boot0_write(true, isp->port.user_context);
    isp->port.nrst_write(false, isp->port.user_context);
    isp->port.delay_ms(STM32_ISP_RESET_PULSE_MS, isp->port.user_context);
    isp->port.nrst_write(true, isp->port.user_context);
    isp->port.delay_ms(STM32_ISP_BOOT_WAIT_MS, isp->port.user_context);
    return STM32_ISP_OK;
}

stm32_isp_status_t stm32_isp_sync(stm32_isp_t *isp)
{
    uint8_t sync = STM32_ISP_SYNC_BYTE;
    stm32_isp_status_t status;

    if (isp == NULL)
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    isp->synchronized = false;
    status            = uart_write(isp, &sync, 1u, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = wait_ack(isp, isp->command_timeout_ms);
    if (status == STM32_ISP_OK)
    {
        isp->synchronized = true;
    }
    return status;
}

stm32_isp_status_t stm32_isp_get_commands(stm32_isp_t *isp, uint8_t *bootloader_version,
                                          uint8_t *commands, size_t command_capacity,
                                          size_t *command_count)
{
    uint8_t count_minus_one;
    uint8_t response[256];
    size_t response_length;
    size_t available_commands;
    size_t copy_count;
    stm32_isp_status_t status;

    if ((isp == NULL) || (bootloader_version == NULL) || (command_count == NULL) ||
        ((commands == NULL) && (command_capacity != 0u)))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    status = send_command(isp, STM32_ISP_CMD_GET);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = uart_read(isp, &count_minus_one, 1u, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }

    response_length = (size_t) count_minus_one + 1u;
    status          = uart_read(isp, response, response_length, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = wait_ack(isp, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }

    *bootloader_version = response[0];
    available_commands  = response_length - 1u;
    *command_count      = available_commands;
    copy_count = (available_commands < command_capacity) ? available_commands : command_capacity;
    if ((commands != NULL) && (copy_count != 0u))
    {
        memcpy(commands, &response[1], copy_count);
    }

    isp->supports_read_memory =
        command_present(&response[1], available_commands, STM32_ISP_CMD_READ_MEMORY);
    isp->supports_erase = command_present(&response[1], available_commands, STM32_ISP_CMD_ERASE);
    isp->supports_extended_erase =
        command_present(&response[1], available_commands, STM32_ISP_CMD_EXTENDED_ERASE);
    isp->commands_known = true;
    return STM32_ISP_OK;
}

stm32_isp_status_t stm32_isp_get_id(stm32_isp_t *isp, uint16_t *product_id)
{
    uint8_t count_minus_one;
    uint8_t id[256];
    size_t id_length;
    stm32_isp_status_t status;

    if ((isp == NULL) || (product_id == NULL))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    status = send_command(isp, STM32_ISP_CMD_GET_ID);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = uart_read(isp, &count_minus_one, 1u, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    id_length = (size_t) count_minus_one + 1u;
    status    = uart_read(isp, id, id_length, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = wait_ack(isp, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    if (id_length < 2u)
    {
        return STM32_ISP_ERROR_PROTOCOL;
    }

    *product_id = (uint16_t) (((uint16_t) id[id_length - 2u] << 8) | id[id_length - 1u]);
    return STM32_ISP_OK;
}

stm32_isp_status_t stm32_isp_read_memory(stm32_isp_t *isp, uint32_t address, uint8_t *data,
                                         size_t length)
{
    uint8_t length_packet[2];
    stm32_isp_status_t status;

    if ((isp == NULL) || (data == NULL) || (length == 0u) || (length > STM32_ISP_MAX_WRITE_SIZE))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }
    if (isp->commands_known && !isp->supports_read_memory)
    {
        return STM32_ISP_ERROR_UNSUPPORTED;
    }

    status = send_command(isp, STM32_ISP_CMD_READ_MEMORY);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = send_address(isp, address);
    if (status != STM32_ISP_OK)
    {
        return status;
    }

    length_packet[0] = (uint8_t) (length - 1u);
    length_packet[1] = (uint8_t) (length_packet[0] ^ 0xFFu);
    status = uart_write(isp, length_packet, sizeof(length_packet), isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = wait_ack(isp, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return uart_read(isp, data, length, isp->command_timeout_ms);
}

stm32_isp_status_t stm32_isp_write_memory(stm32_isp_t *isp, uint32_t address, const uint8_t *data,
                                          size_t length)
{
    uint8_t packet[STM32_ISP_MAX_WRITE_SIZE + 2u];
    uint8_t checksum;
    size_t i;
    stm32_isp_status_t status;

    if ((isp == NULL) || (data == NULL) || (length == 0u) || (length > STM32_ISP_MAX_WRITE_SIZE))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    status = send_command(isp, STM32_ISP_CMD_WRITE_MEMORY);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = send_address(isp, address);
    if (status != STM32_ISP_OK)
    {
        return status;
    }

    packet[0] = (uint8_t) (length - 1u);
    checksum  = packet[0];
    for (i = 0u; i < length; ++i)
    {
        packet[i + 1u] = data[i];
        checksum ^= data[i];
    }
    packet[length + 1u] = checksum;

    status = uart_write(isp, packet, length + 2u, isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return wait_ack(isp, isp->command_timeout_ms);
}

static stm32_isp_status_t extended_mass_erase(stm32_isp_t *isp)
{
    const uint8_t packet[3] = {0xFFu, 0xFFu, 0x00u};
    stm32_isp_status_t status;

    status = send_command(isp, STM32_ISP_CMD_EXTENDED_ERASE);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = uart_write(isp, packet, sizeof(packet), isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return wait_ack(isp, isp->erase_timeout_ms);
}

static stm32_isp_status_t standard_mass_erase(stm32_isp_t *isp)
{
    const uint8_t packet[2] = {0xFFu, 0x00u};
    stm32_isp_status_t status;

    status = send_command(isp, STM32_ISP_CMD_ERASE);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    status = uart_write(isp, packet, sizeof(packet), isp->command_timeout_ms);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return wait_ack(isp, isp->erase_timeout_ms);
}

stm32_isp_status_t stm32_isp_mass_erase(stm32_isp_t *isp)
{
    stm32_isp_status_t status;

    if (isp == NULL)
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    if (isp->commands_known)
    {
        if (isp->supports_extended_erase)
        {
            return extended_mass_erase(isp);
        }
        if (isp->supports_erase)
        {
            return standard_mass_erase(isp);
        }
        return STM32_ISP_ERROR_UNSUPPORTED;
    }

    /* Capabilities were not queried: try the modern command, then legacy. */
    status = extended_mass_erase(isp);
    if (status == STM32_ISP_ERROR_NACK)
    {
        status = standard_mass_erase(isp);
    }
    return status;
}

stm32_isp_status_t stm32_isp_go(stm32_isp_t *isp, uint32_t address)
{
    stm32_isp_status_t status;

    if (isp == NULL)
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    status = send_command(isp, STM32_ISP_CMD_GO);
    if (status != STM32_ISP_OK)
    {
        return status;
    }
    return send_address(isp, address);
}

stm32_isp_status_t stm32_isp_program_image(stm32_isp_t *isp, uint32_t start_address,
                                           const uint8_t *image, size_t image_size,
                                           bool verify_after_write)
{
    uint8_t write_buffer[STM32_ISP_MAX_WRITE_SIZE];
    uint8_t verify_buffer[STM32_ISP_MAX_WRITE_SIZE];
    uint32_t address;
    size_t offset = 0u;
    size_t source_length;
    size_t write_length;
    stm32_isp_status_t status;

    if ((isp == NULL) || (image == NULL) || (image_size == 0u) || ((start_address & 3u) != 0u) ||
        (image_size > (size_t) (UINT32_MAX - start_address)))
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    while (offset < image_size)
    {
        source_length = image_size - offset;
        if (source_length > STM32_ISP_MAX_WRITE_SIZE)
        {
            source_length = STM32_ISP_MAX_WRITE_SIZE;
        }

        write_length = (source_length + 3u) & ~(size_t) 3u;
        memset(write_buffer, 0xFF, write_length);
        memcpy(write_buffer, &image[offset], source_length);
        address = start_address + (uint32_t) offset;

        status = stm32_isp_write_memory(isp, address, write_buffer, write_length);
        if (status != STM32_ISP_OK)
        {
            return status;
        }

        if (verify_after_write)
        {
            status = stm32_isp_read_memory(isp, address, verify_buffer, write_length);
            if (status != STM32_ISP_OK)
            {
                return status;
            }
            if (memcmp(write_buffer, verify_buffer, write_length) != 0)
            {
                return STM32_ISP_ERROR_VERIFY;
            }
        }

        offset += source_length;
    }

    return STM32_ISP_OK;
}

stm32_isp_status_t stm32_isp_reset_to_flash(stm32_isp_t *isp)
{
    if (isp == NULL)
    {
        return STM32_ISP_ERROR_INVALID_ARGUMENT;
    }

    isp->synchronized = false;
    isp->port.boot0_write(false, isp->port.user_context);
    isp->port.nrst_write(false, isp->port.user_context);
    isp->port.delay_ms(STM32_ISP_RESET_PULSE_MS, isp->port.user_context);
    isp->port.nrst_write(true, isp->port.user_context);
    isp->port.delay_ms(STM32_ISP_BOOT_WAIT_MS, isp->port.user_context);
    return STM32_ISP_OK;
}

const char *stm32_isp_status_string(stm32_isp_status_t status)
{
    switch (status)
    {
        case STM32_ISP_OK:
            return "ok";
        case STM32_ISP_ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case STM32_ISP_ERROR_IO:
            return "I/O error";
        case STM32_ISP_ERROR_TIMEOUT:
            return "timeout";
        case STM32_ISP_ERROR_NACK:
            return "target returned NACK";
        case STM32_ISP_ERROR_PROTOCOL:
            return "protocol error";
        case STM32_ISP_ERROR_UNSUPPORTED:
            return "command unsupported";
        case STM32_ISP_ERROR_VERIFY:
            return "read-back verification failed";
        case STM32_ISP_ERROR_NOT_SYNCHRONIZED:
            return "bootloader not synchronized";
        default:
            return "unknown error";
    }
}
