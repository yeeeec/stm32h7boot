#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "spi_nor.h"

#define COMMAND_WRITE_ENABLE        0x06U
#define COMMAND_READ_STATUS         0x05U
#define COMMAND_READ_JEDEC_ID       0x9FU
#define COMMAND_FAST_READ           0x0BU
#define COMMAND_PAGE_PROGRAM        0x02U
#define COMMAND_SECTOR_ERASE        0x20U
#define COMMAND_ENTER_4BYTE_ADDRESS 0xB7U
#define STATUS_BUSY                 0x01U
#define STATUS_WRITE_ENABLE_LATCH   0x02U

typedef struct
{
    uint32_t now_ms;
    uint8_t status_register;
    uint8_t jedec_id[SPI_NOR_JEDEC_ID_SIZE];
    uint8_t last_instruction;
    uint8_t last_address_bytes;
    uint32_t last_address;
    uint32_t program_sizes[4];
    uint32_t program_count;
    uint32_t erase_count;
    uint32_t read_count;
    uint32_t busy_reads_remaining;
    int entered_4byte_address_mode;
    int stuck_busy;
} fake_port_t;

static void StartBusy(fake_port_t *fake)
{
    fake->status_register &= (uint8_t)~STATUS_WRITE_ENABLE_LATCH;
    fake->status_register |= STATUS_BUSY;
    fake->busy_reads_remaining = 2U;
}

static firmware_status_t FakeCommand(
    void *context,
    const spi_nor_transaction_t *transaction)
{
    fake_port_t *fake = (fake_port_t *)context;

    fake->last_instruction = transaction->instruction;
    fake->last_address = transaction->address;
    fake->last_address_bytes = transaction->address_bytes;

    if (transaction->instruction == COMMAND_WRITE_ENABLE)
    {
        fake->status_register |= STATUS_WRITE_ENABLE_LATCH;
    }
    else if (transaction->instruction == COMMAND_ENTER_4BYTE_ADDRESS)
    {
        fake->entered_4byte_address_mode = 1;
    }
    else if (transaction->instruction == COMMAND_SECTOR_ERASE)
    {
        if ((fake->status_register & STATUS_WRITE_ENABLE_LATCH) == 0U)
        {
            return FIRMWARE_STATUS_IO_ERROR;
        }
        ++fake->erase_count;
        StartBusy(fake);
    }

    return FIRMWARE_STATUS_OK;
}

static firmware_status_t FakeReceive(
    void *context,
    const spi_nor_transaction_t *transaction,
    uint8_t *data,
    uint32_t size)
{
    fake_port_t *fake = (fake_port_t *)context;

    fake->last_instruction = transaction->instruction;
    fake->last_address = transaction->address;
    fake->last_address_bytes = transaction->address_bytes;

    if ((transaction->instruction == COMMAND_READ_JEDEC_ID) &&
        (size == SPI_NOR_JEDEC_ID_SIZE))
    {
        memcpy(data, fake->jedec_id, size);
        return FIRMWARE_STATUS_OK;
    }
    if ((transaction->instruction == COMMAND_READ_STATUS) && (size == 1U))
    {
        if ((fake->stuck_busy == 0) && (fake->busy_reads_remaining > 0U))
        {
            --fake->busy_reads_remaining;
            if (fake->busy_reads_remaining == 0U)
            {
                fake->status_register &= (uint8_t)~STATUS_BUSY;
            }
        }
        data[0] = fake->status_register;
        return FIRMWARE_STATUS_OK;
    }
    if (transaction->instruction == COMMAND_FAST_READ)
    {
        ++fake->read_count;
        memset(data, 0xA5, size);
        return FIRMWARE_STATUS_OK;
    }

    return FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t FakeTransmit(
    void *context,
    const spi_nor_transaction_t *transaction,
    const uint8_t *data,
    uint32_t size)
{
    fake_port_t *fake = (fake_port_t *)context;

    (void)data;
    if ((transaction->instruction != COMMAND_PAGE_PROGRAM) ||
        ((fake->status_register & STATUS_WRITE_ENABLE_LATCH) == 0U) ||
        (fake->program_count >= 4U))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    fake->last_instruction = transaction->instruction;
    fake->last_address = transaction->address;
    fake->last_address_bytes = transaction->address_bytes;
    fake->program_sizes[fake->program_count] = size;
    ++fake->program_count;
    StartBusy(fake);
    return FIRMWARE_STATUS_OK;
}

static uint32_t FakeNowMs(void *context)
{
    return ((fake_port_t *)context)->now_ms;
}

static void FakeDelayMs(void *context, uint32_t delay_ms)
{
    ((fake_port_t *)context)->now_ms += delay_ms;
}

static spi_nor_port_t MakePort(fake_port_t *fake)
{
    spi_nor_port_t port;

    port.context = fake;
    port.command = FakeCommand;
    port.receive = FakeReceive;
    port.transmit = FakeTransmit;
    port.now_ms = FakeNowMs;
    port.delay_ms = FakeDelayMs;
    port.poll_hook = NULL;
    port.max_transfer_size = 16U;
    return port;
}

static void TestDeviceOperations(void)
{
    fake_port_t fake = {0};
    spi_nor_t device = {0};
    spi_nor_port_t port = MakePort(&fake);
    spi_nor_config_t config = {0};
    spi_nor_info_t info;
    uint8_t buffer[20];

    fake.jedec_id[0] = 0xEFU;
    fake.jedec_id[1] = 0x40U;
    fake.jedec_id[2] = 0x1AU;

    assert(SpiNor_Init(&device, &port, &config) == FIRMWARE_STATUS_OK);
    assert(fake.entered_4byte_address_mode != 0);
    assert(SpiNor_GetInfo(&device, &info) == FIRMWARE_STATUS_OK);
    assert(info.capacity_bytes == (64UL * 1024UL * 1024UL));
    assert(info.page_size == 256U);
    assert(info.erase_size == 4096U);

    assert(SpiNor_Read(&device, 0x01000010UL, buffer, sizeof(buffer)) ==
           FIRMWARE_STATUS_OK);
    assert(fake.last_instruction == COMMAND_FAST_READ);
    assert(fake.last_address_bytes == 4U);
    assert(fake.read_count == 2U);
    assert(buffer[0] == 0xA5U);

    assert(SpiNor_Program(&device, 250U, buffer, sizeof(buffer)) ==
           FIRMWARE_STATUS_OK);
    assert(fake.program_count == 2U);
    assert(fake.program_sizes[0] == 6U);
    assert(fake.program_sizes[1] == 14U);

    assert(SpiNor_Erase(&device, 1U, 4096U) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(SpiNor_Erase(&device, 0U, 8192U) == FIRMWARE_STATUS_OK);
    assert(fake.erase_count == 2U);
    assert(SpiNor_Read(&device, info.capacity_bytes - 4U, buffer, 8U) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);
}

static void TestEraseTimeout(void)
{
    fake_port_t fake = {0};
    spi_nor_t device = {0};
    spi_nor_port_t port = MakePort(&fake);
    spi_nor_config_t config = {
        .program_timeout_ms = 2U,
        .erase_timeout_ms = 2U,
    };

    fake.jedec_id[0] = 0xEFU;
    fake.jedec_id[1] = 0x40U;
    fake.jedec_id[2] = 0x18U;
    assert(SpiNor_Init(&device, &port, &config) == FIRMWARE_STATUS_OK);

    fake.stuck_busy = 1;
    assert(SpiNor_Erase(&device, 0U, 4096U) == FIRMWARE_STATUS_TIMEOUT);
}

static void TestAsynchronousErase(void)
{
    fake_port_t fake = {0};
    spi_nor_t device = {0};
    spi_nor_port_t port = MakePort(&fake);
    spi_nor_config_t config = {0};
    spi_nor_operation_result_t result;

    fake.jedec_id[0] = 0xEFU;
    fake.jedec_id[1] = 0x40U;
    fake.jedec_id[2] = 0x18U;
    assert(SpiNor_Init(&device, &port, &config) == FIRMWARE_STATUS_OK);

    assert(SpiNor_EraseStart(&device, 0U, 4096U) == FIRMWARE_STATUS_OK);
    assert(SpiNor_GetOperationResult(&device, &result) == FIRMWARE_STATUS_OK);
    assert(result.state == SPI_NOR_OPERATION_BUSY);
    assert(SpiNor_EraseStart(&device, 4096U, 4096U) ==
           FIRMWARE_STATUS_INVALID_STATE);

    assert(SpiNor_OperationPoll(&device) == FIRMWARE_STATUS_OK);
    assert(SpiNor_GetOperationResult(&device, &result) == FIRMWARE_STATUS_OK);
    assert(result.state == SPI_NOR_OPERATION_BUSY);
    assert(SpiNor_OperationPoll(&device) == FIRMWARE_STATUS_OK);
    assert(SpiNor_GetOperationResult(&device, &result) == FIRMWARE_STATUS_OK);
    assert(result.state == SPI_NOR_OPERATION_SUCCEEDED);
    assert(result.status == FIRMWARE_STATUS_OK);
}

static void TestAsynchronousProgram(void)
{
    fake_port_t fake = {0};
    spi_nor_t device = {0};
    spi_nor_port_t port = MakePort(&fake);
    spi_nor_config_t config = {0};
    spi_nor_operation_result_t result;
    uint8_t data[20] = {0};

    fake.jedec_id[0] = 0xEFU;
    fake.jedec_id[1] = 0x40U;
    fake.jedec_id[2] = 0x18U;
    assert(SpiNor_Init(&device, &port, &config) == FIRMWARE_STATUS_OK);

    assert(SpiNor_ProgramStart(&device, 250U, data, sizeof(data)) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(SpiNor_ProgramStart(&device, 0U, data, sizeof(data)) ==
           FIRMWARE_STATUS_OK);
    assert(SpiNor_OperationPoll(&device) == FIRMWARE_STATUS_OK);
    assert(SpiNor_OperationPoll(&device) == FIRMWARE_STATUS_OK);
    assert(SpiNor_GetOperationResult(&device, &result) == FIRMWARE_STATUS_OK);
    assert(result.state == SPI_NOR_OPERATION_SUCCEEDED);
}

int main(void)
{
    TestDeviceOperations();
    TestEraseTimeout();
    TestAsynchronousErase();
    TestAsynchronousProgram();
    return 0;
}
