#include "platform/platform_flash.h"

#include <stddef.h>

#include "bsp/bsp_qspi_flash.h"
#include "platform/platform_system.h"
#include "quadspi.h"
#include "spi_nor.h"

#define PLATFORM_FLASH_TRANSFER_MAX       4096U
#define PLATFORM_FLASH_PROGRAM_TIMEOUT_MS 1000U
#define PLATFORM_FLASH_ERASE_TIMEOUT_MS   5000U
#define W25Q_COMMAND_FAST_READ            0x0BU
#define W25Q_FAST_READ_DUMMY_CYCLES       8U

static spi_nor_t s_flash;
static uint8_t s_initialized;
static uint8_t s_memory_mapped;

static spi_nor_status_t MapFirmwareToSpiNor(firmware_status_t status)
{
    switch (status)
    {
        case FIRMWARE_STATUS_OK:
            return SPI_NOR_STATUS_OK;
        case FIRMWARE_STATUS_INVALID_ARGUMENT:
            return SPI_NOR_STATUS_INVALID_ARGUMENT;
        case FIRMWARE_STATUS_INVALID_STATE:
            return SPI_NOR_STATUS_INVALID_STATE;
        case FIRMWARE_STATUS_OUT_OF_RANGE:
            return SPI_NOR_STATUS_OUT_OF_RANGE;
        case FIRMWARE_STATUS_TIMEOUT:
            return SPI_NOR_STATUS_TIMEOUT;
        case FIRMWARE_STATUS_NOT_SUPPORTED:
            return SPI_NOR_STATUS_NOT_SUPPORTED;
        default:
            return SPI_NOR_STATUS_IO_ERROR;
    }
}

static firmware_status_t MapSpiNorStatus(spi_nor_status_t status)
{
    switch (status)
    {
        case SPI_NOR_STATUS_OK:
            return FIRMWARE_STATUS_OK;
        case SPI_NOR_STATUS_INVALID_ARGUMENT:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        case SPI_NOR_STATUS_INVALID_STATE:
            return FIRMWARE_STATUS_INVALID_STATE;
        case SPI_NOR_STATUS_OUT_OF_RANGE:
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        case SPI_NOR_STATUS_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;
        case SPI_NOR_STATUS_NOT_SUPPORTED:
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        case SPI_NOR_STATUS_IO_ERROR:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static firmware_status_t MapHalStatus(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:
            return FIRMWARE_STATUS_OK;
        case HAL_BUSY:
            return FIRMWARE_STATUS_BUSY;
        case HAL_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;
        case HAL_ERROR:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static void CopyTransaction(const spi_nor_transaction_t *source,
                            bsp_qspi_flash_transaction_t *destination)
{
    destination->instruction   = source->instruction;
    destination->address_bytes = source->address_bytes;
    destination->dummy_cycles  = source->dummy_cycles;
    destination->address       = source->address;
}

static spi_nor_status_t QspiCommand(void *context, const spi_nor_transaction_t *transaction)
{
    bsp_qspi_flash_transaction_t bsp_transaction;

    (void) context;
    if (transaction == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return MapFirmwareToSpiNor(BspQspiFlash_Command(&bsp_transaction));
}

static spi_nor_status_t QspiReceive(void *context, const spi_nor_transaction_t *transaction,
                                    uint8_t *data, uint32_t size)
{
    bsp_qspi_flash_transaction_t bsp_transaction;

    (void) context;
    if (transaction == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return MapFirmwareToSpiNor(BspQspiFlash_Receive(&bsp_transaction, data, size));
}

static spi_nor_status_t QspiTransmit(void *context, const spi_nor_transaction_t *transaction,
                                     const uint8_t *data, uint32_t size)
{
    bsp_qspi_flash_transaction_t bsp_transaction;

    (void) context;
    if (transaction == NULL)
    {
        return SPI_NOR_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return MapFirmwareToSpiNor(BspQspiFlash_Transmit(&bsp_transaction, data, size));
}

static uint32_t NowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

static void DelayMs(void *context, uint32_t delay_ms)
{
    (void) context;
    PlatformSystem_DelayMs(delay_ms);
}

static firmware_status_t WaitOperation(void)
{
    spi_nor_operation_result_t result;
    spi_nor_status_t status;

    for (;;)
    {
        status = SpiNor_OperationPoll(&s_flash);
        if (status != SPI_NOR_STATUS_OK)
        {
            return MapSpiNorStatus(status);
        }

        status = SpiNor_GetOperationResult(&s_flash, &result);
        if (status != SPI_NOR_STATUS_OK)
        {
            return MapSpiNorStatus(status);
        }
        if (result.state == SPI_NOR_OPERATION_SUCCEEDED)
        {
            return FIRMWARE_STATUS_OK;
        }
        if (result.state == SPI_NOR_OPERATION_FAILED)
        {
            return MapSpiNorStatus(result.status);
        }
        if (result.state != SPI_NOR_OPERATION_BUSY)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }

        PlatformSystem_WatchdogRefresh();
        PlatformSystem_DelayMs(1U);
    }
}

static void SynchronizeCaches(void)
{
#if (__DCACHE_PRESENT == 1U)
    SCB_CleanInvalidateDCache();
#endif
#if (__ICACHE_PRESENT == 1U)
    SCB_InvalidateICache();
#endif
    __DSB();
    __ISB();
}

firmware_status_t PlatformFlash_Init(void)
{
    const spi_nor_port_t port     = {.context           = NULL,
                                     .command           = QspiCommand,
                                     .receive           = QspiReceive,
                                     .transmit          = QspiTransmit,
                                     .now_ms            = NowMs,
                                     .delay_ms          = DelayMs,
                                     .max_transfer_size = PLATFORM_FLASH_TRANSFER_MAX};
    const spi_nor_config_t config = {.program_timeout_ms = PLATFORM_FLASH_PROGRAM_TIMEOUT_MS,
                                     .erase_timeout_ms   = PLATFORM_FLASH_ERASE_TIMEOUT_MS};
    spi_nor_info_t info;
    spi_nor_status_t status;

    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    MX_QUADSPI_Init();
    status = SpiNor_Init(&s_flash, &port, &config);
    if (status != SPI_NOR_STATUS_OK)
    {
        return MapSpiNorStatus(status);
    }

    status = SpiNor_GetInfo(&s_flash, &info);
    if (status != SPI_NOR_STATUS_OK)
    {
        return MapSpiNorStatus(status);
    }
    if ((info.capacity_bytes != PLATFORM_FLASH_CAPACITY_BYTES) ||
        (info.page_size != PLATFORM_FLASH_PAGE_SIZE) ||
        (info.erase_size != PLATFORM_FLASH_ERASE_SIZE) ||
        (info.jedec_id[0] != SPI_NOR_W25Q_MANUFACTURER_ID) ||
        (info.jedec_id[1] != SPI_NOR_W25Q256_MEMORY_TYPE) ||
        (info.jedec_id[2] != SPI_NOR_W25Q256_CAPACITY_ID))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_GetInfo(platform_flash_info_t *info)
{
    spi_nor_info_t driver_info;
    spi_nor_status_t status;

    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = SpiNor_GetInfo(&s_flash, &driver_info);
    if (status != SPI_NOR_STATUS_OK)
    {
        return MapSpiNorStatus(status);
    }

    info->capacity_bytes = driver_info.capacity_bytes;
    info->page_size      = driver_info.page_size;
    info->erase_size     = driver_info.erase_size;
    info->jedec_id[0]    = driver_info.jedec_id[0];
    info->jedec_id[1]    = driver_info.jedec_id[1];
    info->jedec_id[2]    = driver_info.jedec_id[2];
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_Read(uint32_t address, void *buffer, uint32_t size)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapSpiNorStatus(SpiNor_Read(&s_flash, address, buffer, size));
}

firmware_status_t PlatformFlash_Write(uint32_t address, const void *buffer, uint32_t size)
{
    const uint8_t *source = (const uint8_t *) buffer;
    uint32_t remaining    = size;

    if ((buffer == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((s_initialized == 0U) || (s_memory_mapped != 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((size > PLATFORM_FLASH_CAPACITY_BYTES) ||
        (address > (PLATFORM_FLASH_CAPACITY_BYTES - size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    while (remaining != 0U)
    {
        uint32_t page_remaining = PLATFORM_FLASH_PAGE_SIZE - (address % PLATFORM_FLASH_PAGE_SIZE);
        uint32_t chunk          = (remaining < page_remaining) ? remaining : page_remaining;
        spi_nor_status_t status = SpiNor_ProgramStart(&s_flash, address, source, chunk);

        if (status != SPI_NOR_STATUS_OK)
        {
            return MapSpiNorStatus(status);
        }
        {
            firmware_status_t wait_status = WaitOperation();
            if (wait_status != FIRMWARE_STATUS_OK)
            {
                return wait_status;
            }
        }

        address += chunk;
        source += chunk;
        remaining -= chunk;
    }

    SynchronizeCaches();
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_Erase(uint32_t address, uint32_t size)
{
    uint32_t remaining = size;

    if ((size == 0U) || ((address % PLATFORM_FLASH_ERASE_SIZE) != 0U) ||
        ((size % PLATFORM_FLASH_ERASE_SIZE) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((s_initialized == 0U) || (s_memory_mapped != 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((size > PLATFORM_FLASH_CAPACITY_BYTES) ||
        (address > (PLATFORM_FLASH_CAPACITY_BYTES - size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    while (remaining != 0U)
    {
        spi_nor_status_t status = SpiNor_EraseStart(&s_flash, address, PLATFORM_FLASH_ERASE_SIZE);

        if (status != SPI_NOR_STATUS_OK)
        {
            return MapSpiNorStatus(status);
        }
        {
            firmware_status_t wait_status = WaitOperation();
            if (wait_status != FIRMWARE_STATUS_OK)
            {
                return wait_status;
            }
        }

        address += PLATFORM_FLASH_ERASE_SIZE;
        remaining -= PLATFORM_FLASH_ERASE_SIZE;
        PlatformSystem_WatchdogRefresh();
    }

    SynchronizeCaches();
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_EnterMemoryMapped(void)
{
    QSPI_CommandTypeDef command            = {0};
    QSPI_MemoryMappedTypeDef memory_mapped = {0};
    firmware_status_t status;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    command.Instruction       = W25Q_COMMAND_FAST_READ;
    command.AddressMode       = QSPI_ADDRESS_1_LINE;
    command.AddressSize       = QSPI_ADDRESS_32_BITS;
    command.Address           = 0U;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode          = QSPI_DATA_1_LINE;
    command.DummyCycles       = W25Q_FAST_READ_DUMMY_CYCLES;
    command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    memory_mapped.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    memory_mapped.TimeOutPeriod     = 0U;

    status = MapHalStatus(HAL_QSPI_MemoryMapped(&hqspi, &command, &memory_mapped));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    SynchronizeCaches();
    s_memory_mapped = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_ExitMemoryMapped(void)
{
    firmware_status_t status;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped == 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    status = MapHalStatus(HAL_QSPI_Abort(&hqspi));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    SynchronizeCaches();
    s_memory_mapped = 0U;
    return FIRMWARE_STATUS_OK;
}

int PlatformFlash_IsMemoryMapped(void)
{
    return (s_memory_mapped != 0U) ? 1 : 0;
}
