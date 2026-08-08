/**
 * @file xip_controller.h
 * @brief External-flash memory-mapped execution control contract.
 */
#ifndef FIRMWARE_XIP_CONTROLLER_H
#define FIRMWARE_XIP_CONTROLLER_H

#include <stdint.h>

#include "firmware/status.h"

typedef firmware_status_t (*xip_controller_enter_fn)(void *context);
typedef firmware_status_t (*xip_controller_exit_fn)(void *context);
typedef firmware_status_t (*xip_controller_is_mapped_fn)(
    void *context,
    int *mapped);
typedef firmware_status_t (*xip_controller_invalidate_fn)(
    void *context,
    uint32_t mapped_address,
    uint32_t size);

/** Exclusive controller for the external-flash memory-mapped window. */
typedef struct
{
    void *context; /**< Provider-owned peripheral state. */
    /** Enter read-only memory-mapped mode from indirect mode. */
    xip_controller_enter_fn enter_memory_mapped_read;
    /** Return to indirect mode before erase or program operations. */
    xip_controller_exit_fn exit_memory_mapped;
    /**
     * Query whether the memory-mapped window is actually active in the peripheral.
     * Implementations must not report only a stale software ownership cache.
     */
    xip_controller_is_mapped_fn is_memory_mapped;
    /** Invalidate data and instruction cache state for a mapped byte range. */
    xip_controller_invalidate_fn invalidate_mapped_cache;
} xip_controller_t;

#endif
