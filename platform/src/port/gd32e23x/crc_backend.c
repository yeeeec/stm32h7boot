/**
 * @file crc_backend.c
 * @brief GD32E23x hardware CRC backend implementation.
 */
#include "gd32e23x.h"

#include "platform/crc.h"

/**
 * @brief Initialize CRC peripheral.
 */
void platform_crc_init(void) {
    rcu_periph_clock_enable(RCU_CRC);

    crc_deinit();

    crc_input_data_reverse_config(CRC_INPUT_DATA_WORD);

    crc_reverse_output_data_enable();

    crc_data_register_reset();
}

/**
 * @brief Reset CRC calculation unit to the initial value.
 */
void crc_reset_unit(void) {
    crc_data_register_reset();
}

/**
 * @brief Calculate CRC for 32-bit word input.
 *
 * @param data Pointer to 32-bit input words.
 * @param length Number of 32-bit words.
 * @return CRC value, or 0 on invalid input.
 */
uint32_t crc_calculate_words(const uint32_t *data, size_t length) {
    if (!data || length == 0) {
        return 0;
    }

    crc_reset_unit();

    for (size_t i = 0; i < length; ++i) {
        crc_single_data_calculate(data[i], INPUT_FORMAT_WORD);
    }

    return crc_data_register_read();
}

/**
 * @brief Calculate CRC for byte input.
 *
 * Hardware CRC expects 32-bit writes, so bytes are packed into 32-bit words
 * (little endian) before feeding the CRC engine.
 *
 * @param data Pointer to input bytes.
 * @param length Number of bytes.
 * @return CRC value, or 0 on invalid input.
 */
uint32_t crc_calculate_bytes(const uint8_t *data, size_t length) {
    if (!data || length == 0) {
        return 0;
    }

    crc_reset_unit();

    while (length >= 4) {
        uint32_t word = ((uint32_t) data[0]) | ((uint32_t) data[1] << 8) |
                        ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);

        crc_single_data_calculate(word, INPUT_FORMAT_WORD);

        data += 4;
        length -= 4;
    }

    if (length > 0) {
        uint32_t last = 0;
        for (size_t i = 0; i < length; ++i) {
            last |= ((uint32_t) data[i]) << (8U * i);
        }
        crc_single_data_calculate(last, INPUT_FORMAT_WORD);
    }

    uint32_t crc = crc_data_register_read();

    return crc ^ 0xFFFFFFFFU;
}
