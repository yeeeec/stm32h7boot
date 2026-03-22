/**
 * @file iflash_backend.c
 * @brief GD32E23x internal flash backend implementation.
 */
#include "gd32e23x.h"

#include "platform/iflash.h"
#include <string.h>

static inline void iflash_unlock(void);
static inline void iflash_lock(void);
static inline void iflash_clear_flags(void);
static Plat_Status_t iflash_program_word_no_irq(uint32_t addr, uint32_t data);

/**
 * @brief Unlock flash controller.
 */
static inline void iflash_unlock(void) {
    fmc_unlock();
}

/**
 * @brief Lock flash controller.
 */
static inline void iflash_lock(void) {
    fmc_lock();
}

/**
 * @brief Clear flash status flags.
 */
static inline void iflash_clear_flags(void) {
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_WPERR | FMC_FLAG_PGERR);
}

/**
 * @brief Program a single 32-bit word with a critical section.
 *
 * GD32E230 programs by word (32-bit). Interrupts are disabled to prevent vector
 * fetch from flash while the controller is busy.
 *
 * @param addr Destination address (word-aligned).
 * @param data 32-bit word to program.
 * @return Platform status code.
 */
static Plat_Status_t iflash_program_word_no_irq(uint32_t addr, uint32_t data) {
    __disable_irq();

    iflash_clear_flags();

    fmc_state_enum state = fmc_word_program(addr, data);

    iflash_clear_flags();

    __enable_irq();

    if (state != FMC_READY) {
        return PLAT_ERR_HW_FAILURE;
    }

    if (*(volatile uint32_t *) addr != data) {
        return PLAT_ERR_HW_FAILURE;
    }

    return PLAT_OK;
}

/**
 * @brief Initialize internal flash driver.
 */
void platform_flash_init(void) {
    iflash_unlock();
    iflash_clear_flags();
    iflash_lock();
}

/**
 * @brief Read flash memory.
 *
 * @param addr Flash address.
 * @param buf Destination buffer.
 * @param len Length in bytes.
 */
void platform_flash_read(uint32_t addr, void *buf, uint32_t len) {
    memcpy(buf, (void *) addr, len);
}

/**
 * @brief Erase flash pages covering the given range.
 *
 * @param start_addr Start address.
 * @param length Length in bytes.
 * @return Platform status code.
 */
Plat_Status_t platform_flash_erase(uint32_t start_addr, uint32_t length) {
    if (length == 0) {
        return PLAT_OK;
    }

    uint32_t page_start = (start_addr / PLAT_FLASH_PAGE_SIZE) * PLAT_FLASH_PAGE_SIZE;
    uint32_t page_end = ((start_addr + length + PLAT_FLASH_PAGE_SIZE - 1) / PLAT_FLASH_PAGE_SIZE) *
                        PLAT_FLASH_PAGE_SIZE;

    iflash_unlock();
    iflash_clear_flags();

    Plat_Status_t status = PLAT_OK;

    for (uint32_t addr = page_start; addr < page_end; addr += PLAT_FLASH_PAGE_SIZE) {
        __disable_irq();
        fmc_state_enum state = fmc_page_erase(addr);
        iflash_clear_flags();
        __enable_irq();

        if (state != FMC_READY) {
            status = PLAT_ERR_HW_FAILURE;
            break;
        }
    }

    iflash_lock();
    return status;
}

/**
 * @brief Write data to flash memory using read-modify-write on 32-bit words.
 *
 * This driver assumes the target area is already erased (0xFFFFFFFF).
 *
 * @param addr Destination address.
 * @param data Source data buffer.
 * @param len Length in bytes.
 * @return Platform status code.
 */
Plat_Status_t platform_flash_write(uint32_t addr, const void *data, uint32_t len) {
    if (len == 0) {
        return PLAT_OK;
    }

    const uint8_t *p_src  = (const uint8_t *) data;
    uint32_t current_addr = addr;
    uint32_t end_addr     = addr + len;
    Plat_Status_t status  = PLAT_OK;

    iflash_unlock();

    while (current_addr < end_addr) {
        uint32_t offset            = current_addr & 0x03U;
        uint32_t word_aligned_addr = current_addr & ~0x03U;

        uint32_t flash_word   = *(volatile uint32_t *) word_aligned_addr;
        uint32_t new_word     = flash_word;
        uint8_t *p_word_bytes = (uint8_t *) &new_word;

        uint32_t bytes_to_write = 4U - offset;
        if (current_addr + bytes_to_write > end_addr) {
            bytes_to_write = end_addr - current_addr;
        }

        for (uint32_t i = 0; i < bytes_to_write; i++) {
            p_word_bytes[offset + i] = *p_src++;
        }

        status = iflash_program_word_no_irq(word_aligned_addr, new_word);
        if (status != PLAT_OK) {
            break;
        }

        current_addr += bytes_to_write;
    }

    iflash_lock();
    return status;
}
