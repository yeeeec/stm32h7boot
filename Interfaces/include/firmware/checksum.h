/**
 * @file checksum.h
 * @brief Incremental checksum capability required by firmware services.
 */
#ifndef FIRMWARE_CHECKSUM_H
#define FIRMWARE_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

typedef firmware_status_t (*checksum_reset_fn)(void *context);
typedef firmware_status_t (*checksum_update_fn)(
    void *context,
    const void *data,
    size_t size);
typedef firmware_status_t (*checksum_get_value_fn)(
    void *context,
    uint32_t *value);

/**
 * The provider owns its mutable calculation context. Callers must not interleave
 * two calculations through the same interface instance.
 */
typedef struct
{
    void *context; /**< Provider-owned mutable calculation state. */
    checksum_reset_fn reset; /**< Begin a new calculation. */
    checksum_update_fn update; /**< Consume bytes without retaining the buffer. */
    checksum_get_value_fn get_value; /**< Return the current finalized value. */
} checksum_t;

#endif
