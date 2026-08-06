/**
 * @file system_reset.h
 * @brief Platform-independent system reset contract.
 */
#ifndef FIRMWARE_SYSTEM_RESET_H
#define FIRMWARE_SYSTEM_RESET_H

/** Request an immediate whole-system reset. A provider must not return. */
typedef void (*system_reset_request_fn)(void *context);

typedef struct
{
    void *context; /**< Provider-owned context passed to request. */
    system_reset_request_fn request; /**< Request an immediate reset. */
} system_reset_t;

#endif
