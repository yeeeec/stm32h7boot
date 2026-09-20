/**
 * @file spi_nor.c
 * @brief Winbond W25Qx SPI-NOR implementation; default target W25Q256.
 */
#include "spi_nor.h"

#include <stddef.h>

#define W25Q_COMMAND_WRITE_ENABLE        0x06U
#define W25Q_COMMAND_READ_STATUS         0x05U
#define W25Q_COMMAND_READ_JEDEC_ID       0x9FU
#define W25Q_COMMAND_FAST_READ           0x0BU
#define W25Q_COMMAND_PAGE_PROGRAM        0x02U
#define W25Q_COMMAND_SECTOR_ERASE        0x20U
#define W25Q_COMMAND_ENTER_4BYTE_ADDRESS 0xB7U
#define W25Q_COMMAND_RESET_ENABLE        0x66U
#define W25Q_COMMAND_RESET               0x99U

#define W25Q_STATUS_BUSY                0x01U
#define W25Q_STATUS_WRITE_ENABLE_LATCH  0x02U
#define W25Q_3BYTE_CAPACITY_LIMIT       0x01000000UL
#define W25Q_MIN_DENSITY_CODE           0x11U
#define W25Q_MAX_DENSITY_CODE           SPI_NOR_W25Q256_CAPACITY_ID
#define W25Q_DEFAULT_PROGRAM_TIMEOUT_MS 1000U
#define W25Q_DEFAULT_ERASE_TIMEOUT_MS   5000U

static spi_nor_transaction_t Transaction(uint8_t instruction, uint32_t address,
                                         uint8_t address_bytes, uint8_t dummy_cycles)
{
    spi_nor_transaction_t transaction;

    transaction.instruction   = instruction;
    transaction.address       = address;
    transaction.address_bytes = address_bytes;
    transaction.dummy_cycles  = dummy_cycles;
    return transaction;
}

static spi_nor_status_t ValidateRange(const spi_nor_t *device, uint32_t address, uint32_t size)
{
    if ((device == NULL) || (size == 0U))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    if ((size > device->info.capacity_bytes) || (address > (device->info.capacity_bytes - size)))
    {
        return SPI_NOR_STATUS_OUT_OF_RANGE;
    }
    return SPI_NOR_STATUS_OK;
}

static spi_nor_status_t ReadStatus(spi_nor_t *device, uint8_t *status_register)
{
    spi_nor_transaction_t transaction = Transaction(W25Q_COMMAND_READ_STATUS, 0U, 0U, 0U);
    return device->port.receive(device->port.context, &transaction, status_register, 1U);
}

static spi_nor_status_t WaitReady(spi_nor_t *device, uint32_t timeout_ms)
{
    uint32_t start_ms = device->port.now_ms(device->port.context);

    for (;;)
    {
        uint8_t status_register;
        spi_nor_status_t status = ReadStatus(device, &status_register);

        if (status != SPI_NOR_STATUS_OK)
        {
            return status;
        }
        if ((status_register & W25Q_STATUS_BUSY) == 0U)
        {
            return SPI_NOR_STATUS_OK;
        }
        if ((uint32_t) (device->port.now_ms(device->port.context) - start_ms) >= timeout_ms)
        {
            return SPI_NOR_STATUS_TIMEOUT;
        }
        device->port.delay_ms(device->port.context, 1U);
    }
}

static spi_nor_status_t WriteEnable(spi_nor_t *device)
{
    spi_nor_transaction_t transaction = Transaction(W25Q_COMMAND_WRITE_ENABLE, 0U, 0U, 0U);
    uint8_t status_register;
    spi_nor_status_t status = device->port.command(device->port.context, &transaction);

    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    status = ReadStatus(device, &status_register);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    return ((status_register & W25Q_STATUS_WRITE_ENABLE_LATCH) != 0U) ? SPI_NOR_STATUS_OK
                                                                      : SPI_NOR_STATUS_IO_ERROR;
}

static spi_nor_status_t SetOperationFailure(spi_nor_t *device, spi_nor_status_t status)
{
    device->operation_state  = SPI_NOR_OPERATION_FAILED;
    device->operation_status = status;
    return status;
}

spi_nor_status_t SpiNor_Init(spi_nor_t *device, const spi_nor_port_t *port,
                             const spi_nor_config_t *config)
{
    spi_nor_transaction_t transaction;
    uint8_t density_code;
    spi_nor_status_t status;

    if ((device == NULL) || (port == NULL) || (config == NULL) || (port->command == NULL) ||
        (port->receive == NULL) || (port->transmit == NULL) || (port->now_ms == NULL) ||
        (port->delay_ms == NULL) || (port->max_transfer_size == 0U))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized != 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }

    device->port               = *port;
    device->program_timeout_ms = (config->program_timeout_ms == 0U)
                                     ? W25Q_DEFAULT_PROGRAM_TIMEOUT_MS
                                     : config->program_timeout_ms;
    device->erase_timeout_ms =
        (config->erase_timeout_ms == 0U) ? W25Q_DEFAULT_ERASE_TIMEOUT_MS : config->erase_timeout_ms;

    transaction = Transaction(W25Q_COMMAND_RESET_ENABLE, 0U, 0U, 0U);
    status      = device->port.command(device->port.context, &transaction);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    transaction = Transaction(W25Q_COMMAND_RESET, 0U, 0U, 0U);
    status      = device->port.command(device->port.context, &transaction);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    device->port.delay_ms(device->port.context, 1U);

    transaction = Transaction(W25Q_COMMAND_READ_JEDEC_ID, 0U, 0U, 0U);
    status      = device->port.receive(device->port.context, &transaction, device->info.jedec_id,
                                       SPI_NOR_JEDEC_ID_SIZE);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }

    density_code = device->info.jedec_id[2];
    if ((device->info.jedec_id[0] != SPI_NOR_W25Q_MANUFACTURER_ID) ||
        (density_code < W25Q_MIN_DENSITY_CODE) || (density_code > W25Q_MAX_DENSITY_CODE))
    {
        return SPI_NOR_STATUS_NOT_SUPPORTED;
    }

    device->info.capacity_bytes = 1UL << density_code;
    device->info.page_size      = SPI_NOR_W25Q_PAGE_SIZE_BYTES;
    device->info.erase_size     = SPI_NOR_W25Q_SECTOR_SIZE_BYTES;
    device->address_bytes    = (device->info.capacity_bytes > W25Q_3BYTE_CAPACITY_LIMIT) ? 4U : 3U;
    device->operation_state  = SPI_NOR_OPERATION_IDLE;
    device->operation_status = SPI_NOR_STATUS_OK;

    if (device->address_bytes == 4U)
    {
        transaction = Transaction(W25Q_COMMAND_ENTER_4BYTE_ADDRESS, 0U, 0U, 0U);
        status      = device->port.command(device->port.context, &transaction);
        if (status != SPI_NOR_STATUS_OK)
        {
            return status;
        }
    }

    status = WaitReady(device, device->program_timeout_ms);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }

    device->initialized = 1;
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_GetInfo(const spi_nor_t *device, spi_nor_info_t *info)
{
    if ((device == NULL) || (info == NULL))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    *info = device->info;
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_GetMemoryMappedReadTransaction(const spi_nor_t *device,
                                                       spi_nor_transaction_t *transaction)
{
    if ((device == NULL) || (transaction == NULL))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    *transaction = Transaction(W25Q_COMMAND_FAST_READ, 0U, device->address_bytes, 8U);
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_Read(spi_nor_t *device, uint32_t address, void *data, uint32_t size)
{
    uint8_t *destination = (uint8_t *) data;
    uint32_t remaining   = size;
    spi_nor_status_t status;

    if (data == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if ((device != NULL) && (device->operation_state == SPI_NOR_OPERATION_BUSY))
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    status = ValidateRange(device, address, size);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }

    while (remaining > 0U)
    {
        uint32_t chunk_size = (remaining < device->port.max_transfer_size)
                                  ? remaining
                                  : device->port.max_transfer_size;
        spi_nor_transaction_t transaction =
            Transaction(W25Q_COMMAND_FAST_READ, address, device->address_bytes, 8U);

        status = device->port.receive(device->port.context, &transaction, destination, chunk_size);
        if (status != SPI_NOR_STATUS_OK)
        {
            return status;
        }
        address += chunk_size;
        destination += chunk_size;
        remaining -= chunk_size;
    }
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_ProgramStart(spi_nor_t *device, uint32_t address, const void *data,
                                     uint32_t size)
{
    spi_nor_transaction_t transaction;
    spi_nor_status_t status;

    if (data == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    status = ValidateRange(device, address, size);
    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    if (device->operation_state == SPI_NOR_OPERATION_BUSY)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    if ((size > device->info.page_size) || (size > device->port.max_transfer_size) ||
        ((address % device->info.page_size) > (device->info.page_size - size)))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }

    status = WriteEnable(device);
    if (status != SPI_NOR_STATUS_OK)
    {
        return SetOperationFailure(device, status);
    }
    transaction = Transaction(W25Q_COMMAND_PAGE_PROGRAM, address, device->address_bytes, 0U);
    status =
        device->port.transmit(device->port.context, &transaction, (const uint8_t *) data, size);
    if (status != SPI_NOR_STATUS_OK)
    {
        return SetOperationFailure(device, status);
    }

    device->operation_started_ms = device->port.now_ms(device->port.context);
    device->operation_timeout_ms = device->program_timeout_ms;
    device->operation_status     = SPI_NOR_STATUS_OK;
    device->operation_state      = SPI_NOR_OPERATION_BUSY;
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_EraseStart(spi_nor_t *device, uint32_t address, uint32_t size)
{
    spi_nor_transaction_t transaction;
    spi_nor_status_t status = ValidateRange(device, address, size);

    if (status != SPI_NOR_STATUS_OK)
    {
        return status;
    }
    if (device->operation_state == SPI_NOR_OPERATION_BUSY)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    if (((address % device->info.erase_size) != 0U) || (size != device->info.erase_size))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }

    status = WriteEnable(device);
    if (status != SPI_NOR_STATUS_OK)
    {
        return SetOperationFailure(device, status);
    }
    transaction = Transaction(W25Q_COMMAND_SECTOR_ERASE, address, device->address_bytes, 0U);
    status      = device->port.command(device->port.context, &transaction);
    if (status != SPI_NOR_STATUS_OK)
    {
        return SetOperationFailure(device, status);
    }

    device->operation_started_ms = device->port.now_ms(device->port.context);
    device->operation_timeout_ms = device->erase_timeout_ms;
    device->operation_status     = SPI_NOR_STATUS_OK;
    device->operation_state      = SPI_NOR_OPERATION_BUSY;
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_OperationPoll(spi_nor_t *device)
{
    uint8_t status_register;
    spi_nor_status_t status;

    if (device == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    if (device->operation_state != SPI_NOR_OPERATION_BUSY)
    {
        return SPI_NOR_STATUS_OK;
    }

    status = ReadStatus(device, &status_register);
    if (status != SPI_NOR_STATUS_OK)
    {
        return SetOperationFailure(device, status);
    }
    if ((status_register & W25Q_STATUS_BUSY) == 0U)
    {
        device->operation_state  = SPI_NOR_OPERATION_SUCCEEDED;
        device->operation_status = SPI_NOR_STATUS_OK;
        return SPI_NOR_STATUS_OK;
    }
    if ((uint32_t) (device->port.now_ms(device->port.context) - device->operation_started_ms) >=
        device->operation_timeout_ms)
    {
        return SetOperationFailure(device, SPI_NOR_STATUS_TIMEOUT);
    }
    return SPI_NOR_STATUS_OK;
}

spi_nor_status_t SpiNor_GetOperationResult(const spi_nor_t *device,
                                           spi_nor_operation_result_t *result)
{
    if ((device == NULL) || (result == NULL))
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    if (device->initialized == 0)
    {
        return SPI_NOR_STATUS_INVALID_STATE;
    }
    result->state  = device->operation_state;
    result->status = device->operation_status;
    return SPI_NOR_STATUS_OK;
}
