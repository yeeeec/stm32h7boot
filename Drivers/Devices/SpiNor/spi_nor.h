/**
 * @file spi_nor.h
 * @brief Hardware-independent Winbond W25Qx SPI-NOR driver.
 *
 * The default target is W25Q256 (JEDEC ID EF 40 19). The driver accepts
 * Winbond W25Q devices with density codes up to W25Q256 and uses the common
 * 256-byte page, 4-KiB sector and standard single-line command set.
 */
#ifndef SPI_NOR_H
#define SPI_NOR_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SPI_NOR_JEDEC_ID_SIZE          3U
#define SPI_NOR_W25Q_MANUFACTURER_ID   0xEFU
#define SPI_NOR_W25Q256_MEMORY_TYPE    0x40U
#define SPI_NOR_W25Q256_CAPACITY_ID    0x19U
#define SPI_NOR_W25Q256_CAPACITY_BYTES (32UL * 1024UL * 1024UL)
#define SPI_NOR_W25Q_PAGE_SIZE_BYTES   256U
#define SPI_NOR_W25Q_SECTOR_SIZE_BYTES 4096U

    typedef firmware_status_t spi_nor_status_t;
#define SPI_NOR_STATUS_OK               FIRMWARE_STATUS_OK
#define SPI_NOR_STATUS_INVALID_ARGUMENT FIRMWARE_STATUS_INVALID_ARGUMENT
#define SPI_NOR_STATUS_INVALID_STATE    FIRMWARE_STATUS_INVALID_STATE
#define SPI_NOR_STATUS_OUT_OF_RANGE     FIRMWARE_STATUS_OUT_OF_RANGE
#define SPI_NOR_STATUS_IO_ERROR         FIRMWARE_STATUS_IO_ERROR
#define SPI_NOR_STATUS_TIMEOUT          FIRMWARE_STATUS_TIMEOUT
#define SPI_NOR_STATUS_NOT_SUPPORTED    FIRMWARE_STATUS_NOT_SUPPORTED

    typedef struct
    {
        uint8_t instruction;
        uint8_t address_bytes;
        uint8_t dummy_cycles;
        uint32_t address;
    } spi_nor_transaction_t;

    typedef spi_nor_status_t (*spi_nor_command_fn)(void *context,
                                                   const spi_nor_transaction_t *transaction);
    typedef spi_nor_status_t (*spi_nor_receive_fn)(void *context,
                                                   const spi_nor_transaction_t *transaction,
                                                   uint8_t *data, uint32_t size);
    typedef spi_nor_status_t (*spi_nor_transmit_fn)(void *context,
                                                    const spi_nor_transaction_t *transaction,
                                                    const uint8_t *data, uint32_t size);
    typedef uint32_t (*spi_nor_now_ms_fn)(void *context);
    typedef void (*spi_nor_delay_ms_fn)(void *context, uint32_t delay_ms);

    typedef struct
    {
        void *context;
        spi_nor_command_fn command;
        spi_nor_receive_fn receive;
        spi_nor_transmit_fn transmit;
        spi_nor_now_ms_fn now_ms;
        spi_nor_delay_ms_fn delay_ms;
        uint32_t max_transfer_size;
    } spi_nor_port_t;

    typedef struct
    {
        /** Zero selects the W25Q256-oriented conservative default. */
        uint32_t program_timeout_ms;
        uint32_t erase_timeout_ms;
    } spi_nor_config_t;

    typedef struct
    {
        uint8_t jedec_id[SPI_NOR_JEDEC_ID_SIZE];
        uint32_t capacity_bytes;
        uint32_t page_size;
        uint32_t erase_size;
    } spi_nor_info_t;

    typedef enum
    {
        SPI_NOR_OPERATION_IDLE = 0,
        SPI_NOR_OPERATION_BUSY,
        SPI_NOR_OPERATION_SUCCEEDED,
        SPI_NOR_OPERATION_FAILED
    } spi_nor_operation_state_t;

    typedef struct
    {
        spi_nor_operation_state_t state;
        spi_nor_status_t status;
    } spi_nor_operation_result_t;

    typedef struct
    {
        spi_nor_port_t port;
        spi_nor_info_t info;
        uint32_t program_timeout_ms;
        uint32_t erase_timeout_ms;
        uint32_t operation_started_ms;
        uint32_t operation_timeout_ms;
        spi_nor_status_t operation_status;
        spi_nor_operation_state_t operation_state;
        uint8_t address_bytes;
        int initialized;
    } spi_nor_t;

    spi_nor_status_t SpiNor_Init(spi_nor_t *device, const spi_nor_port_t *port,
                                 const spi_nor_config_t *config);
    spi_nor_status_t SpiNor_GetInfo(const spi_nor_t *device, spi_nor_info_t *info);
    spi_nor_status_t SpiNor_GetMemoryMappedReadTransaction(const spi_nor_t *device,
                                                           spi_nor_transaction_t *transaction);
    spi_nor_status_t SpiNor_Read(spi_nor_t *device, uint32_t address, void *data, uint32_t size);

    /** Start one page-bounded, transfer-size-bounded program operation. */
    spi_nor_status_t SpiNor_ProgramStart(spi_nor_t *device, uint32_t address, const void *data,
                                         uint32_t size);

    /** Start one 4-KiB sector erase operation. */
    spi_nor_status_t SpiNor_EraseStart(spi_nor_t *device, uint32_t address, uint32_t size);

    /** Poll the current program or erase operation once without blocking. */
    spi_nor_status_t SpiNor_OperationPoll(spi_nor_t *device);
    spi_nor_status_t SpiNor_GetOperationResult(const spi_nor_t *device,
                                               spi_nor_operation_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
