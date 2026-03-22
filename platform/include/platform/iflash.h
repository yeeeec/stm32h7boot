/** platform/include/platform/iflash.h */
#ifndef PLATFORM_IFLASH_H
#define PLATFORM_IFLASH_H

#include <stdint.h>

#include <stddef.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- Configuration ---------------- */
/** flash base size. */
#define IFLASH_FLASH_BASE_SIZE 0x10000U
/** @deprecated Use IFLASH_FLASH_BASE_SIZE. */
#define PLAT_FLASH_BASE_SIZE IFLASH_FLASH_BASE_SIZE
/** flash page size. */
#define IFLASH_FLASH_PAGE_SIZE 0x400U
/** @deprecated Use IFLASH_FLASH_PAGE_SIZE. */
#define PLAT_FLASH_PAGE_SIZE IFLASH_FLASH_PAGE_SIZE
/** Define start address for parameter storage if needed */
/** flash base addr. */
#define IFLASH_FLASH_BASE_ADDR 0x08000000U
/** @deprecated Use IFLASH_FLASH_BASE_ADDR. */
#define PLAT_FLASH_BASE_ADDR IFLASH_FLASH_BASE_ADDR

/** ---------------- API Functions ---------------- */

/**
 * @brief Init device.
 *
 */
void platform_flash_init(void);

/**
 * @brief Read data.
 *
 * @param addr
 * @param buf
 * @param len
 */
void platform_flash_read(uint32_t addr, void *buf, uint32_t len);

/**
 * @brief Erase pages.
 *
 * @param start_addr
 * @param length
 * @return
 */
Plat_Status_t platform_flash_erase(uint32_t start_addr, uint32_t length);

/**
 * @brief Write data.
 *
 * @param addr
 * @param data
 * @param len
 * @return
 */
Plat_Status_t platform_flash_write(uint32_t addr, const void *data, uint32_t len);


#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_IFLASH_H */