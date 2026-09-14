/**
 * @file spi_nor.h
 * @brief Hardware-independent JEDEC SPI-NOR read/program/erase driver.
 *
 * The driver emits standard single-line SPI-NOR commands through an injected
 * port. It never includes HAL headers and does not know the QSPI instance,
 * board wiring, partitions, image formats, or update policy.
 */
#ifndef DEVICE_SPI_NOR_H
#define DEVICE_SPI_NOR_H

#include <stdint.h>

#include "firmware/status.h"

#define SPI_NOR_JEDEC_ID_SIZE 3U

/* Description of one command phase followed by optional transmit/receive data. */
typedef struct
{
    uint8_t instruction;
    uint8_t address_bytes;
    uint8_t dummy_cycles;
    uint32_t address;
} spi_nor_transaction_t;

/*
 * All bus callbacks are synchronous and must copy data before returning.
 * command handles transactions without a data phase; receive and transmit
 * handle exactly size bytes after sending the command/address phase.
 */
typedef firmware_status_t (*spi_nor_command_fn)(void *context,
                                                const spi_nor_transaction_t *transaction);
typedef firmware_status_t (*spi_nor_receive_fn)(void *context,
                                                const spi_nor_transaction_t *transaction,
                                                uint8_t *data, uint32_t size);
typedef firmware_status_t (*spi_nor_transmit_fn)(void *context,
                                                 const spi_nor_transaction_t *transaction,
                                                 const uint8_t *data, uint32_t size);
typedef uint32_t (*spi_nor_now_ms_fn)(void *context);
typedef void (*spi_nor_delay_ms_fn)(void *context, uint32_t delay_ms);
typedef void (*spi_nor_poll_hook_fn)(void *context);

typedef struct
{
    /* Passed unchanged to every callback; ownership remains with the caller. */
    void *context;
    spi_nor_command_fn command;
    spi_nor_receive_fn receive;
    spi_nor_transmit_fn transmit;
    spi_nor_now_ms_fn now_ms;
    spi_nor_delay_ms_fn delay_ms;
    /* Optional hook for watchdog/service polling during long erase waits. */
    spi_nor_poll_hook_fn poll_hook;
    /* Maximum bytes accepted by one receive/transmit callback invocation. */
    uint32_t max_transfer_size;
} spi_nor_port_t;

typedef struct
{
    /* A zero value selects the driver's conservative default timeout. */
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
    firmware_status_t status;
} spi_nor_operation_result_t;

typedef struct spi_nor
{
    spi_nor_port_t port;
    spi_nor_info_t info;
    uint32_t program_timeout_ms;
    uint32_t erase_timeout_ms;
    uint32_t operation_started_ms;
    uint32_t operation_timeout_ms;
    firmware_status_t operation_status;
    spi_nor_operation_state_t operation_state;
    uint8_t address_bytes;
    int initialized;
} spi_nor_t;

/**
 * Reset and identify a device, derive capacity from the JEDEC density byte,
 * and enter four-byte address mode when capacity exceeds 16 MiB.
 * The device object must be zero-initialized and remain valid for all calls.
 */
firmware_status_t SpiNor_Init(spi_nor_t *device, const spi_nor_port_t *port,
                              const spi_nor_config_t *config);
firmware_status_t SpiNor_GetInfo(const spi_nor_t *device, spi_nor_info_t *info);
/* Read accepts arbitrary in-range addresses and splits large bus transfers. */
firmware_status_t SpiNor_Read(spi_nor_t *device, uint32_t address, void *data, uint32_t size);
/**
 * Program arbitrary in-range data using page-sized transactions.
 * The caller must erase the destination beforehand; programming can only
 * change erased bits from one to zero.
 */
firmware_status_t SpiNor_Program(spi_nor_t *device, uint32_t address, const void *data,
                                 uint32_t size);

/**
 * @brief Start one page-bounded program operation without waiting.
 *
 * @param[in,out] device Initialized device retaining operation state.
 * @param[in] address Destination byte offset.
 * @param[in] data Source copied to the QSPI peripheral before this call returns.
 * @param[in] size Nonzero size that must fit in the current program page.
 *
 * @return FIRMWARE_STATUS_OK when the page-program command is accepted.
 * @return FIRMWARE_STATUS_INVALID_STATE while another operation is busy.
 * @return A range, alignment, or transport failure otherwise.
 */
firmware_status_t SpiNor_ProgramStart(spi_nor_t *device, uint32_t address, const void *data,
                                      uint32_t size);
/* Address and size must both be aligned to info.erase_size. */
firmware_status_t SpiNor_Erase(spi_nor_t *device, uint32_t address, uint32_t size);

/**
 * @brief Start one erase-unit operation without waiting for completion.
 *
 * @param[in,out] device Initialized device retaining operation state.
 * @param[in] address Erase-aligned byte offset.
 * @param[in] size Must equal the detected erase size.
 *
 * @return FIRMWARE_STATUS_OK when the erase command is accepted.
 * @return FIRMWARE_STATUS_INVALID_STATE while another erase is busy.
 * @return A range or transport failure otherwise.
 */
firmware_status_t SpiNor_EraseStart(spi_nor_t *device, uint32_t address, uint32_t size);

/** Poll a started erase once without delaying or busy-waiting. */
firmware_status_t SpiNor_OperationPoll(spi_nor_t *device);

/** Return the most recent asynchronous operation state and terminal status. */
firmware_status_t SpiNor_GetOperationResult(const spi_nor_t *device,
                                            spi_nor_operation_result_t *result);

#endif
