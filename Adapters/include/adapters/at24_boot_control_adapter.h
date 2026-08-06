/**
 * @file at24_boot_control_adapter.h
 * @brief Adapter from the AT24 driver to Boot Control persistent storage.
 */
#ifndef ADAPTERS_AT24_BOOT_CONTROL_ADAPTER_H
#define ADAPTERS_AT24_BOOT_CONTROL_ADAPTER_H

#include "firmware/boot_control_store.h"

struct at24;

/** Boot Control interface bound to a caller-owned AT24 driver. */
typedef struct
{
    boot_control_store_t interface;
    struct at24 *device;
} at24_boot_control_adapter_t;

/**
 * @brief Bind an initialized AT24 device to the Boot Control store interface.
 *
 * @param[out] adapter Adapter object to initialize.
 * @param[in] device Initialized device retained by reference.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for a NULL argument.
 * @return A driver state failure if @p device is not initialized.
 */
firmware_status_t At24BootControlAdapter_Init(
    at24_boot_control_adapter_t *adapter,
    struct at24 *device);

/** Return the Boot Control store interface owned by an adapter. */
const boot_control_store_t *At24BootControlAdapter_Interface(
    const at24_boot_control_adapter_t *adapter);

#endif
