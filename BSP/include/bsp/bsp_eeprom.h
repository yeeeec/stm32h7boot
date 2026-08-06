/**
 * @file bsp_eeprom.h
 * @brief Board I2C binding for an AT24C128 EEPROM.
 */
#ifndef BSP_EEPROM_H
#define BSP_EEPROM_H

#include <stdint.h>

#include "firmware/status.h"

struct at24;

/** Board-selectable EEPROM address and driver timeout. */
typedef struct
{
    uint8_t device_address_7bit; /**< Schematic-derived address in 0x50..0x57. */
    uint32_t write_timeout_ms; /**< Maximum AT24 internal write-cycle time. */
} bsp_eeprom_config_t;

/**
 * @brief Bind I2C1 to the board AT24C128 device.
 *
 * @param[in] config Address and timeout confirmed for the populated board.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for a NULL configuration.
 * @return FIRMWARE_STATUS_INVALID_STATE if I2C1 is reset or already bound.
 * @return A driver validation status otherwise.
 *
 * @note WP is not controlled until a board GPIO binding is defined. The
 *       populated board must keep the EEPROM writable when this API is used.
 */
firmware_status_t BSP_EepromInit(const bsp_eeprom_config_t *config);

/** Return the BSP-owned initialized AT24 driver, or NULL before initialization. */
struct at24 *BSP_EepromDevice(void);

#endif
