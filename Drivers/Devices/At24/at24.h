/**
 * @file at24.h
 * @brief Hardware-independent AT24C128 EEPROM device driver.
 *
 * The driver owns AT24C128 geometry, page-boundary enforcement, write-cycle
 * acknowledge polling, and timeout state. Bus and WP operations are injected.
 */
#ifndef DEVICE_AT24_H
#define DEVICE_AT24_H

#include <stdint.h>

#include "firmware/status.h"

#define AT24C128_CAPACITY_BYTES   (16UL * 1024UL)
#define AT24C128_PAGE_SIZE_BYTES  64U
#define AT24C128_ADDRESS_MIN_7BIT 0x50U
#define AT24C128_ADDRESS_MAX_7BIT 0x57U

typedef firmware_status_t (*at24_read_fn)(void *context, uint8_t device_address_7bit,
                                          uint16_t memory_address, uint8_t *data, uint32_t size);
typedef firmware_status_t (*at24_write_fn)(void *context, uint8_t device_address_7bit,
                                           uint16_t memory_address, const uint8_t *data,
                                           uint32_t size);
typedef firmware_status_t (*at24_probe_ready_fn)(void *context, uint8_t device_address_7bit,
                                                 int *ready);
typedef uint32_t (*at24_now_ms_fn)(void *context);
typedef firmware_status_t (*at24_set_write_enabled_fn)(void *context, int enabled);

/** Transport operations supplied by the board binding. */
typedef struct
{
    void *context;                   /**< Passed unchanged to every callback. */
    at24_read_fn read;               /**< Perform one 16-bit-addressed random read. */
    at24_write_fn write;             /**< Transmit one page-bounded write. */
    at24_probe_ready_fn probe_ready; /**< Perform one acknowledge-poll attempt. */
    at24_now_ms_fn now_ms;           /**< Return a wrapping millisecond tick. */
    /** Optional board WP control; NULL means the device is externally writable. */
    at24_set_write_enabled_fn set_write_enabled;
} at24_port_t;

/** AT24C128 board-selectable configuration. */
typedef struct
{
    uint8_t device_address_7bit; /**< Unshifted address in the 0x50..0x57 range. */
    uint32_t write_timeout_ms;   /**< Maximum internal write-cycle duration. */
} at24_config_t;

/** Fixed AT24C128 geometry in bytes. */
typedef struct
{
    uint32_t capacity_bytes;
    uint32_t page_size;
} at24_info_t;

typedef enum
{
    AT24_OPERATION_IDLE = 0,
    AT24_OPERATION_BUSY,
    AT24_OPERATION_SUCCEEDED,
    AT24_OPERATION_FAILED
} at24_operation_state_t;

/** Terminal or in-progress status for a page write. */
typedef struct
{
    at24_operation_state_t state;
    firmware_status_t status;
} at24_operation_result_t;

/** Driver state retained across an AT24 internal write cycle. */
typedef struct at24
{
    at24_port_t port;
    uint32_t write_timeout_ms;
    uint32_t write_started_ms;
    firmware_status_t operation_status;
    at24_operation_state_t operation_state;
    uint8_t device_address_7bit;
    int initialized;
} at24_t;

/**
 * @brief Initialize an AT24C128 device using an injected transport.
 *
 * @param[out] device Zero-initialized driver object.
 * @param[in] port Synchronous transport and optional WP operations.
 * @param[in] config Board address and nonzero write timeout.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for invalid dependencies or address.
 * @return FIRMWARE_STATUS_INVALID_STATE if @p device is already initialized.
 */
firmware_status_t At24_Init(at24_t *device, const at24_port_t *port, const at24_config_t *config);

/** Return the fixed AT24C128 capacity and page size. */
firmware_status_t At24_GetInfo(const at24_t *device, at24_info_t *info);

/** Probe the configured device address once before exposing the store. */
firmware_status_t At24_Probe(at24_t *device);

/** Read an arbitrary nonempty in-range byte sequence while no write is busy. */
firmware_status_t At24_Read(at24_t *device, uint32_t address, void *data, uint32_t size);

/** Start one nonempty write that cannot cross a 64-byte physical page. */
firmware_status_t At24_WritePageStart(at24_t *device, uint32_t address, const void *data,
                                      uint32_t size);

/** Perform one bounded acknowledge-poll attempt for a started write. */
firmware_status_t At24_OperationPoll(at24_t *device);

/** Return the current or most recent write result. */
firmware_status_t At24_GetOperationResult(const at24_t *device, at24_operation_result_t *result);

#endif
