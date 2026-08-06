/**
 * @file stm32_at24_boot_control_adapter.h
 * @brief STM32 I2C AT24 EEPROM adapter for Boot Control persistence.
 */
#ifndef ADAPTERS_STM32_AT24_BOOT_CONTROL_ADAPTER_H
#define ADAPTERS_STM32_AT24_BOOT_CONTROL_ADAPTER_H

#include <stdint.h>

#include "firmware/boot_control_store.h"

struct __I2C_HandleTypeDef;

/** Optional board callback controlling an external EEPROM WP signal. */
typedef firmware_status_t (*at24_set_write_enabled_fn)(
    void *context,
    int enabled);

/** Board and device parameters that cannot be inferred from the I2C peripheral. */
typedef struct
{
    struct __I2C_HandleTypeDef *i2c_handle; /**< Initialized STM32 HAL handle. */
    uint8_t device_address_7bit;            /**< Unshifted AT24 I2C address. */
    uint32_t capacity_bytes;                /**< Addressable EEPROM bytes. */
    uint32_t page_size;                     /**< Physical write-page size in bytes. */
    uint32_t write_timeout_ms;              /**< Maximum acknowledge-poll time. */
    void *write_protect_context;            /**< Passed to set_write_enabled. */
    at24_set_write_enabled_fn set_write_enabled; /**< NULL when WP is not controlled. */
} stm32_at24_boot_control_config_t;

/** Adapter-owned Boot Control interface and EEPROM write-cycle state. */
typedef struct
{
    boot_control_store_t interface;
    struct __I2C_HandleTypeDef *i2c_handle;
    void *write_protect_context;
    at24_set_write_enabled_fn set_write_enabled;
    uint32_t capacity_bytes;
    uint32_t page_size;
    uint32_t write_timeout_ms;
    uint32_t write_started_ms;
    uint16_t hal_device_address;
    int write_pending;
} stm32_at24_boot_control_adapter_t;

/**
 * @brief Bind an AT24 EEPROM to the Boot Control store interface.
 *
 * @param[out] adapter Zero-lifetime adapter object to initialize.
 * @param[in] config Device geometry, address, timeout, and optional WP control.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for missing or invalid parameters.
 * @return FIRMWARE_STATUS_NOT_SUPPORTED if the capacity exceeds 16-bit addressing.
 *
 * @pre The HAL I2C handle remains initialized while the adapter is used.
 * @note A NULL WP callback means the board keeps the EEPROM write-enabled.
 */
firmware_status_t Stm32At24BootControlAdapter_Init(
    stm32_at24_boot_control_adapter_t *adapter,
    const stm32_at24_boot_control_config_t *config);

/**
 * @brief Return the Boot Control store interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return Adapter-owned interface, or NULL for a NULL adapter.
 */
const boot_control_store_t *Stm32At24BootControlAdapter_Interface(
    const stm32_at24_boot_control_adapter_t *adapter);

#endif
