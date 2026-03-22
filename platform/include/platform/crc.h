/** platform/include/platform/crc.h */
#ifndef PLATFORM_CRC_H
#define PLATFORM_CRC_H

#include <stdint.h>

#include <stddef.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- API Functions ---------------- */

/**
 * @brief Init unit.
 *
 */
void platform_crc_init(void);

/**
 * @brief Reset unit.
 *
 */
void crc_reset_unit(void);

/**
 * @brief Calculate words.
 *
 * @param data
 * @param length
 * @return
 */
uint32_t crc_calculate_words(const uint32_t *data, size_t length);

/**
 * @brief Calculate bytes.
 *
 * @param data
 * @param length
 * @return
 */
uint32_t crc_calculate_bytes(const uint8_t *data, size_t length);


#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_CRC_H */