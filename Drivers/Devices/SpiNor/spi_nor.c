/**
 * @file spi_nor.c
 * @brief Standard-command JEDEC SPI-NOR implementation.
 */
#include "spi_nor.h"

#include <stddef.h>

/* Commands shared by common JEDEC-compatible serial NOR devices. */
#define SPI_NOR_COMMAND_WRITE_ENABLE       0x06U
#define SPI_NOR_COMMAND_READ_STATUS        0x05U
#define SPI_NOR_COMMAND_READ_JEDEC_ID      0x9FU
#define SPI_NOR_COMMAND_FAST_READ          0x0BU
#define SPI_NOR_COMMAND_PAGE_PROGRAM       0x02U
#define SPI_NOR_COMMAND_SECTOR_ERASE       0x20U
#define SPI_NOR_COMMAND_ENTER_4BYTE_ADDRESS 0xB7U
#define SPI_NOR_COMMAND_RESET_ENABLE       0x66U
#define SPI_NOR_COMMAND_RESET              0x99U

#define SPI_NOR_STATUS_BUSY                0x01U
#define SPI_NOR_STATUS_WRITE_ENABLE_LATCH  0x02U
#define SPI_NOR_PAGE_SIZE                  256U
#define SPI_NOR_ERASE_SIZE                 4096U
#define SPI_NOR_3BYTE_CAPACITY_LIMIT       0x01000000UL
#define SPI_NOR_DEFAULT_PROGRAM_TIMEOUT_MS 1000U
#define SPI_NOR_DEFAULT_ERASE_TIMEOUT_MS   5000U

static spi_nor_transaction_t Transaction(
    uint8_t instruction,
    uint32_t address,
    uint8_t address_bytes,
    uint8_t dummy_cycles)
{
    spi_nor_transaction_t transaction;

    transaction.instruction = instruction;
    transaction.address = address;
    transaction.address_bytes = address_bytes;
    transaction.dummy_cycles = dummy_cycles;
    return transaction;
}

static firmware_status_t ValidateRange(
    const spi_nor_t *device,
    uint32_t address,
    uint32_t size)
{
    if ((device == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* Subtraction avoids overflow from address + size. */
    if ((size > device->info.capacity_bytes) ||
        (address > (device->info.capacity_bytes - size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    return FIRMWARE_STATUS_OK;
}


static firmware_status_t ReadStatus(spi_nor_t *device, uint8_t *status_register)
{
    spi_nor_transaction_t transaction = Transaction(
        SPI_NOR_COMMAND_READ_STATUS, 0U, 0U, 0U);

    return device->port.receive(
        device->port.context, &transaction, status_register, 1U);
}

static firmware_status_t WaitReady(spi_nor_t *device, uint32_t timeout_ms)
{
    uint32_t start_ms = device->port.now_ms(device->port.context);

    for (;;)
    {
        uint8_t status_register;
        firmware_status_t status = ReadStatus(device, &status_register);

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        if ((status_register & SPI_NOR_STATUS_BUSY) == 0U)
        {
            return FIRMWARE_STATUS_OK;
        }
        if ((uint32_t)(device->port.now_ms(device->port.context) - start_ms) >=
            timeout_ms)
        {
            return FIRMWARE_STATUS_TIMEOUT;
        }

        /* Keep system-critical polling alive during a multi-second erase. */
        if (device->port.poll_hook != NULL)
        {
            device->port.poll_hook(device->port.context);
        }
        device->port.delay_ms(device->port.context, 1U);
    }
}

static firmware_status_t WriteEnable(spi_nor_t *device)
{
    spi_nor_transaction_t transaction = Transaction(
        SPI_NOR_COMMAND_WRITE_ENABLE, 0U, 0U, 0U);
    firmware_status_t status;
    uint8_t status_register;

    status = device->port.command(device->port.context, &transaction);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = ReadStatus(device, &status_register);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    return ((status_register & SPI_NOR_STATUS_WRITE_ENABLE_LATCH) != 0U)
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t SpiNor_Init(
    spi_nor_t *device,
    const spi_nor_port_t *port,
    const spi_nor_config_t *config)
{
    spi_nor_transaction_t transaction;
    uint8_t capacity_exponent;
    firmware_status_t status;

    if ((device == NULL) || (port == NULL) || (config == NULL) ||
        (port->command == NULL) || (port->receive == NULL) ||
        (port->transmit == NULL) || (port->now_ms == NULL) ||
        (port->delay_ms == NULL) || (port->max_transfer_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    device->port = *port;
    device->program_timeout_ms =
        (config->program_timeout_ms == 0U)
            ? SPI_NOR_DEFAULT_PROGRAM_TIMEOUT_MS
            : config->program_timeout_ms;
    device->erase_timeout_ms =
        (config->erase_timeout_ms == 0U)
            ? SPI_NOR_DEFAULT_ERASE_TIMEOUT_MS
            : config->erase_timeout_ms;

    /* Reset first so stale write-enable or address-mode state is not inherited. */
    transaction = Transaction(SPI_NOR_COMMAND_RESET_ENABLE, 0U, 0U, 0U);
    status = device->port.command(device->port.context, &transaction);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    transaction = Transaction(SPI_NOR_COMMAND_RESET, 0U, 0U, 0U);
    status = device->port.command(device->port.context, &transaction);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    device->port.delay_ms(device->port.context, 1U);

    transaction = Transaction(SPI_NOR_COMMAND_READ_JEDEC_ID, 0U, 0U, 0U);
    status = device->port.receive(
        device->port.context,
        &transaction,
        device->info.jedec_id,
        SPI_NOR_JEDEC_ID_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (((device->info.jedec_id[0] == 0U) &&
         (device->info.jedec_id[1] == 0U) &&
         (device->info.jedec_id[2] == 0U)) ||
        ((device->info.jedec_id[0] == 0xFFU) &&
         (device->info.jedec_id[1] == 0xFFU) &&
         (device->info.jedec_id[2] == 0xFFU)))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    /* Standard JEDEC density values encode capacity as 2^N bytes. */
    capacity_exponent = device->info.jedec_id[2];
    if ((capacity_exponent < 16U) || (capacity_exponent > 30U))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    device->info.capacity_bytes = 1UL << capacity_exponent;
    device->info.page_size = SPI_NOR_PAGE_SIZE;
    device->info.erase_size = SPI_NOR_ERASE_SIZE;
    device->address_bytes =
        (device->info.capacity_bytes > SPI_NOR_3BYTE_CAPACITY_LIMIT) ? 4U : 3U;

    /* Three address bytes cover 16 MiB; larger devices need four-byte mode. */
    if (device->address_bytes == 4U)
    {
        transaction = Transaction(
            SPI_NOR_COMMAND_ENTER_4BYTE_ADDRESS, 0U, 0U, 0U);
        status = device->port.command(device->port.context, &transaction);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
    }

    status = WaitReady(device, device->program_timeout_ms);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    device->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t SpiNor_GetInfo(
    const spi_nor_t *device,
    spi_nor_info_t *info)
{
    if ((device == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    *info = device->info;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t SpiNor_Read(
    spi_nor_t *device,
    uint32_t address,
    void *data,
    uint32_t size)
{
    uint8_t *destination = (uint8_t *)data;
    uint32_t remaining = size;
    firmware_status_t range_status;

    if (data == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    range_status = ValidateRange(device, address, size);
    if (!FirmwareStatus_IsOk(range_status))
    {
        return range_status;
    }

    /* Bound each HAL-facing transaction by the port's timeout-safe limit. */
    while (remaining > 0U)
    {
        uint32_t chunk_size = (remaining < device->port.max_transfer_size)
                                  ? remaining
                                  : device->port.max_transfer_size;
        spi_nor_transaction_t transaction = Transaction(
            SPI_NOR_COMMAND_FAST_READ, address, device->address_bytes, 8U);
        firmware_status_t status = device->port.receive(
            device->port.context, &transaction, destination, chunk_size);

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        address += chunk_size;
        destination += chunk_size;
        remaining -= chunk_size;
    }

    return FIRMWARE_STATUS_OK;
}

firmware_status_t SpiNor_Program(
    spi_nor_t *device,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    const uint8_t *source = (const uint8_t *)data;
    uint32_t remaining = size;
    firmware_status_t range_status;

    if (data == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    range_status = ValidateRange(device, address, size);
    if (!FirmwareStatus_IsOk(range_status))
    {
        return range_status;
    }

    /* Never cross a device page boundary in one PAGE_PROGRAM command. */
    while (remaining > 0U)
    {
        uint32_t page_remaining = device->info.page_size -
                                  (address % device->info.page_size);
        uint32_t chunk_size = (remaining < page_remaining)
                                  ? remaining
                                  : page_remaining;
        spi_nor_transaction_t transaction;
        firmware_status_t status = WriteEnable(device);

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        transaction = Transaction(
            SPI_NOR_COMMAND_PAGE_PROGRAM,
            address,
            device->address_bytes,
            0U);
        status = device->port.transmit(
            device->port.context, &transaction, source, chunk_size);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        status = WaitReady(device, device->program_timeout_ms);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        address += chunk_size;
        source += chunk_size;
        remaining -= chunk_size;
    }

    return FIRMWARE_STATUS_OK;
}

firmware_status_t SpiNor_Erase(
    spi_nor_t *device,
    uint32_t address,
    uint32_t size)
{
    uint32_t remaining = size;
    firmware_status_t range_status = ValidateRange(device, address, size);

    if (!FirmwareStatus_IsOk(range_status))
    {
        return range_status;
    }
    /* Sector erase cannot preserve bytes in a partially selected sector. */
    if (((address % device->info.erase_size) != 0U) ||
        ((size % device->info.erase_size) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    while (remaining > 0U)
    {
        spi_nor_transaction_t transaction;
        firmware_status_t status = WriteEnable(device);

        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        transaction = Transaction(
            SPI_NOR_COMMAND_SECTOR_ERASE,
            address,
            device->address_bytes,
            0U);
        status = device->port.command(device->port.context, &transaction);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        status = WaitReady(device, device->erase_timeout_ms);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }

        address += device->info.erase_size;
        remaining -= device->info.erase_size;
    }

    return FIRMWARE_STATUS_OK;
}
