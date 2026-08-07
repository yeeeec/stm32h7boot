/**
 * @file update_request_store.h
 * @brief Fixed trusted-request raw storage contract.
 */
#ifndef FIRMWARE_UPDATE_REQUEST_STORE_H
#define FIRMWARE_UPDATE_REQUEST_STORE_H

#include <stdint.h>

#include "firmware/status.h"

#define UPDATE_REQUEST_STORE_MAX_RAW_SIZE 1024U

typedef firmware_status_t (*update_request_store_load_raw_fn)(
    void *context,
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *size);
typedef firmware_status_t (*update_request_store_clear_fn)(void *context);

/**
 * Raw access to the fixed boot_update_request.json file on an already-mounted
 * volume. This interface never parses JSON or makes trust decisions.
 */
typedef struct
{
    void *context;
    update_request_store_load_raw_fn load_raw;
    update_request_store_clear_fn clear;
} update_request_store_t;

#endif
