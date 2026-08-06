/**
 * @file boot_control_store.h
 * @brief Byte-addressed persistent storage contract for Boot Control records.
 */
#ifndef FIRMWARE_BOOT_CONTROL_STORE_H
#define FIRMWARE_BOOT_CONTROL_STORE_H

#include <stdint.h>

#include "firmware/status.h"

/** EEPROM geometry expressed in bytes. */
typedef struct
{
    uint32_t capacity_bytes; /**< Total byte-addressable capacity. */
    uint32_t page_size;      /**< Physical write-page size. */
} boot_control_store_info_t;

typedef firmware_status_t (*boot_control_store_get_info_fn)(
    void *context,
    boot_control_store_info_t *info);
typedef firmware_status_t (*boot_control_store_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
typedef firmware_status_t (*boot_control_store_write_page_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);
typedef firmware_status_t (*boot_control_store_is_ready_fn)(
    void *context,
    int *ready);

/** Byte-addressed store whose writes complete through readiness polling. */
typedef struct
{
    void *context; /**< Provider-owned context passed to every operation. */
    boot_control_store_get_info_fn get_info; /**< Read immutable device geometry. */
    boot_control_store_read_fn read; /**< Read an in-range byte sequence. */
    /** Start one write that cannot cross a physical page boundary. */
    boot_control_store_write_page_fn write_page;
    /** Poll a started write; no other operation is valid until ready is nonzero. */
    boot_control_store_is_ready_fn is_ready;
} boot_control_store_t;

#endif
