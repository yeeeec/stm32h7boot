#include "platform/platform_flash.h"

#include <stddef.h>

#include "bsp/bsp_qspi_flash.h"
#include "platform/platform_system.h"
#include "spi_nor.h"

#define PLATFORM_FLASH_TRANSFER_MAX       4096U
#define PLATFORM_FLASH_PROGRAM_TIMEOUT_MS 1000U
#define PLATFORM_FLASH_ERASE_TIMEOUT_MS   5000U

static spi_nor_t s_flash;
static uint8_t s_initialized;
static uint8_t s_memory_mapped;

static void CopyTransaction(const spi_nor_transaction_t *source,
                            bsp_qspi_flash_transaction_t *destination)
{
    destination->instruction = source->instruction;
    destination->address_bytes = source->address_bytes;
    destination->dummy_cycles = source->dummy_cycles;
    destination->address = source->address;
}

static firmware_status_t QspiCommand(void *context, const spi_nor_transaction_t *transaction)
{
    bsp_qspi_flash_transaction_t bsp_transaction;
    (void) context;
    if (transaction == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return BspQspiFlash_Command(&bsp_transaction);
}

static firmware_status_t QspiReceive(void *context, const spi_nor_transaction_t *transaction,
                                     uint8_t *data, uint32_t size)
{
    bsp_qspi_flash_transaction_t bsp_transaction;
    (void) context;
    if (transaction == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return BspQspiFlash_Receive(&bsp_transaction, data, size);
}

static firmware_status_t QspiTransmit(void *context, const spi_nor_transaction_t *transaction,
                                      const uint8_t *data, uint32_t size)
{
    bsp_qspi_flash_transaction_t bsp_transaction;
    (void) context;
    if (transaction == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    CopyTransaction(transaction, &bsp_transaction);
    return BspQspiFlash_Transmit(&bsp_transaction, data, size);
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
    for (;;)
    {
        firmware_status_t status = SpiNor_OperationPoll(&s_flash);
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        status = SpiNor_GetOperationResult(&s_flash, &result);
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        if (result.state == SPI_NOR_OPERATION_SUCCEEDED)
        {
            return FIRMWARE_STATUS_OK;
        }
        if (result.state == SPI_NOR_OPERATION_FAILED)
        {
            return result.status;
        }
        if (result.state != SPI_NOR_OPERATION_BUSY)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        PlatformSystem_WatchdogRefresh();
        PlatformSystem_DelayMs(1U);
    }
}

firmware_status_t PlatformFlash_Init(void)
{
    const spi_nor_port_t port = {.context = NULL,
                                 .command = QspiCommand,
                                 .receive = QspiReceive,
                                 .transmit = QspiTransmit,
                                 .now_ms = NowMs,
                                 .delay_ms = DelayMs,
                                 .max_transfer_size = PLATFORM_FLASH_TRANSFER_MAX};
    const spi_nor_config_t config = {.program_timeout_ms = PLATFORM_FLASH_PROGRAM_TIMEOUT_MS,
                                     .erase_timeout_ms = PLATFORM_FLASH_ERASE_TIMEOUT_MS};
    spi_nor_info_t info;
    firmware_status_t status;

    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = BspQspiFlash_Init();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    status = SpiNor_Init(&s_flash, &port, &config);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    status = SpiNor_GetInfo(&s_flash, &info);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    if ((info.capacity_bytes != PLATFORM_FLASH_CAPACITY_BYTES) ||
        (info.page_size != PLATFORM_FLASH_PAGE_SIZE) ||
        (info.erase_size != PLATFORM_FLASH_ERASE_SIZE))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_GetInfo(platform_flash_info_t *info)
{
    spi_nor_info_t driver_info;
    firmware_status_t status;
    if (info == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = SpiNor_GetInfo(&s_flash, &driver_info);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    info->capacity_bytes = driver_info.capacity_bytes;
    info->page_size = driver_info.page_size;
    info->erase_size = driver_info.erase_size;
    info->jedec_id[0] = driver_info.jedec_id[0];
    info->jedec_id[1] = driver_info.jedec_id[1];
    info->jedec_id[2] = driver_info.jedec_id[2];
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_Read(uint32_t address, void *buffer, uint32_t size)
{
    if ((buffer == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((s_initialized == 0U) || (s_memory_mapped != 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return SpiNor_Read(&s_flash, address, buffer, size);
}

firmware_status_t PlatformFlash_Write(uint32_t address, const void *buffer, uint32_t size)
{
    const uint8_t *source = (const uint8_t *) buffer;
    uint32_t remaining = size;
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
        uint32_t chunk = (remaining < page_remaining) ? remaining : page_remaining;
        firmware_status_t status = SpiNor_ProgramStart(&s_flash, address, source, chunk);
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        status = WaitOperation();
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        address += chunk;
        source += chunk;
        remaining -= chunk;
    }
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
        firmware_status_t status = SpiNor_EraseStart(&s_flash, address,
                                                     PLATFORM_FLASH_ERASE_SIZE);
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        status = WaitOperation();
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        address += PLATFORM_FLASH_ERASE_SIZE;
        remaining -= PLATFORM_FLASH_ERASE_SIZE;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformFlash_EnterMemoryMapped(void)
{
    spi_nor_transaction_t driver_transaction;
    bsp_qspi_flash_transaction_t bsp_transaction;
    firmware_status_t status;
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_memory_mapped != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = SpiNor_GetMemoryMappedReadTransaction(&s_flash, &driver_transaction);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    CopyTransaction(&driver_transaction, &bsp_transaction);
    status = BspQspiFlash_EnterMemoryMapped(&bsp_transaction);
    if (status == FIRMWARE_STATUS_OK)
    {
        s_memory_mapped = 1U;
    }
    return status;
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
    status = BspQspiFlash_ExitMemoryMapped();
    if (status == FIRMWARE_STATUS_OK)
    {
        s_memory_mapped = 0U;
    }
    return status;
}

int PlatformFlash_IsMemoryMapped(void)
{
    return (s_memory_mapped != 0U) ? 1 : 0;
}
