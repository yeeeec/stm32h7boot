#include "boot_crc32.h"

#include <string.h>

uint32_t Boot_Crc32_Calc(const void *data, size_t length, uint32_t seed) {
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc         = ~seed;

    if (bytes == NULL) {
        return 0U;
    }

    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

uint32_t Boot_Crc32_IsoUpdate(uint32_t current_crc, const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc         = current_crc ^ 0xFFFFFFFFUL;

    if ((bytes == NULL) && (length != 0U)) {
        return 0U;
    }

    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

uint32_t Boot_Crc32_IsoCalc(const void *data, size_t length) {
    return Boot_Crc32_IsoUpdate(0U, data, length);
}

uint32_t Boot_Crc32_Mpeg2Update(uint32_t current_crc, const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc         = current_crc;
    size_t offset        = 0U;

    if ((bytes == NULL) && (length != 0U)) {
        return 0U;
    }

    while (offset < length) {
        uint8_t word_buffer[4] = {0U, 0U, 0U, 0U};
        uint32_t word;
        size_t chunk_size = length - offset;

        if (chunk_size > sizeof(word_buffer)) {
            chunk_size = sizeof(word_buffer);
        }

        memcpy(word_buffer, &bytes[offset], chunk_size);
        word = ((uint32_t) word_buffer[0]) | ((uint32_t) word_buffer[1] << 8U) |
               ((uint32_t) word_buffer[2] << 16U) | ((uint32_t) word_buffer[3] << 24U);

        crc ^= word;
        for (uint32_t bit = 0; bit < 32U; ++bit) {
            if ((crc & 0x80000000UL) != 0U) {
                crc = (crc << 1U) ^ 0x04C11DB7UL;
            } else {
                crc <<= 1U;
            }
        }

        offset += chunk_size;
    }

    return crc;
}
