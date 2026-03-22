/** platform/include/platform/i2c.h */
#ifndef PLATFORM_I2C_H
#define PLATFORM_I2C_H

#include <stdint.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- API Functions ---------------- */

/**
 * @brief Init bus.
 *
 */
void platform_i2c_init(void);

/**
 * @brief Write it.
 *
 * @param addr7
 * @param buf
 * @param len
 * @return
 */
Plat_Status_t platform_i2c_write_it(uint8_t addr7, const uint8_t *buf, uint16_t len);

/**
 * @brief Read it.
 *
 * @param addr7
 * @param buf
 * @param len
 * @return
 */
Plat_Status_t platform_i2c_read_it(uint8_t addr7, uint8_t *buf, uint16_t len);


/** ---------------- Weak Callbacks ---------------- */
/** Override these in your application to handle completion */

#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_I2C_H */