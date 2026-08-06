/**
 * @file async_block_device.h
 * @brief Bounded block-device access contract for long-running services.
 */
#ifndef FIRMWARE_ASYNC_BLOCK_DEVICE_H
#define FIRMWARE_ASYNC_BLOCK_DEVICE_H

#include <stdint.h>

#include "firmware/status.h"

typedef struct
{
    uint32_t capacity_bytes;
    /* Maximum bytes accepted by one program call; calls may not cross this boundary. */
    uint32_t program_size;
    uint32_t erase_size;
} async_block_device_info_t;

typedef enum
{
    ASYNC_BLOCK_DEVICE_OPERATION_IDLE = 0,
    ASYNC_BLOCK_DEVICE_OPERATION_BUSY,
    ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED,
    ASYNC_BLOCK_DEVICE_OPERATION_FAILED
} async_block_device_operation_state_t;

typedef struct
{
    async_block_device_operation_state_t state;
    firmware_status_t status;
} async_block_device_operation_result_t;

typedef firmware_status_t (*async_block_device_get_info_fn)(
    void *context,
    async_block_device_info_t *info);
typedef firmware_status_t (*async_block_device_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
typedef firmware_status_t (*async_block_device_program_start_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);
typedef firmware_status_t (*async_block_device_erase_start_fn)(
    void *context,
    uint32_t address,
    uint32_t size);
typedef firmware_status_t (*async_block_device_poll_fn)(void *context);
typedef firmware_status_t (*async_block_device_get_operation_result_fn)(
    void *context,
    async_block_device_operation_result_t *result);
typedef firmware_status_t (*async_block_device_cancel_fn)(void *context);

/**
 * Long erase work is split between erase_start and poll. Implementations must
 * make each poll call bounded. A NULL cancel callback explicitly means that an
 * operation cannot be aborted after it has started.
 */
typedef struct
{
    void *context;
    async_block_device_get_info_fn get_info;
    async_block_device_read_fn read;
    async_block_device_program_start_fn program_start;
    async_block_device_erase_start_fn erase_start;
    async_block_device_poll_fn poll;
    async_block_device_get_operation_result_fn get_operation_result;
    async_block_device_cancel_fn cancel;
} async_block_device_t;

#endif
