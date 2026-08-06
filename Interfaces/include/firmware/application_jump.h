/**
 * @file application_jump.h
 * @brief Final Cortex-M application handoff contract.
 */
#ifndef FIRMWARE_APPLICATION_JUMP_H
#define FIRMWARE_APPLICATION_JUMP_H

#include <stdint.h>

#include "firmware/status.h"

typedef firmware_status_t (*application_jump_execute_fn)(
    void *context,
    uint32_t vector_table_address);

/**
 * A successful execute call never returns. Returning FIRMWARE_STATUS_OK is an
 * implementation error and must be converted to a Launch Service failure.
 */
typedef struct
{
    void *context; /**< Provider-owned platform context. */
    application_jump_execute_fn execute; /**< Transfer control using a vector address. */
} application_jump_t;

#endif
