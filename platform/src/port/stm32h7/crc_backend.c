#include "platform/crc.h"

#include "main.h"

#define PLATFORM_CRC_WORD_CHUNK_SIZE 64U

extern CRC_HandleTypeDef hcrc;

static bool platform_crc_ready(void) {
    return hcrc.Instance == CRC;
}

void platform_crc_init(void) {
    hcrc.Instance = CRC;
    hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
    hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
    hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
    hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
    (void)HAL_CRC_Init(&hcrc);
}

void crc_reset_unit(void) {
    if (platform_crc_ready() == false) {
        return;
    }

    __HAL_CRC_DR_RESET(&hcrc);
}

uint32_t crc_calculate_words(const uint32_t *data, size_t length) {
    if ((data == NULL) || (length == 0U) || (length > UINT32_MAX)) {
        return 0U;
    }

    if (platform_crc_ready() == false) {
        platform_crc_init();
    }

    hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
    return HAL_CRC_Calculate(&hcrc, data, (uint32_t)length);
}

uint32_t crc_calculate_bytes(const uint8_t *data, size_t length) {
    uint32_t word_buffer[PLATFORM_CRC_WORD_CHUNK_SIZE];
    const uint8_t *cursor = data;
    size_t remaining = length;
    uint32_t crc = 0U;
    bool first_chunk = true;

    if ((data == NULL) || (length == 0U)) {
        return 0U;
    }

    if (platform_crc_ready() == false) {
        platform_crc_init();
    }

    hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;

    while (remaining > 0U) {
        size_t word_count = 0U;

        while ((word_count < PLATFORM_CRC_WORD_CHUNK_SIZE) && (remaining > 0U)) {
            uint32_t word = 0U;
            size_t copy_size = (remaining >= sizeof(uint32_t)) ? sizeof(uint32_t) : remaining;

            for (size_t index = 0U; index < copy_size; ++index) {
                word |= ((uint32_t)cursor[index]) << (8U * index);
            }

            word_buffer[word_count] = word;
            cursor += copy_size;
            remaining -= copy_size;
            ++word_count;
        }

        if (first_chunk != false) {
            crc = HAL_CRC_Calculate(&hcrc, word_buffer, (uint32_t)word_count);
            first_chunk = false;
        } else {
            crc = HAL_CRC_Accumulate(&hcrc, word_buffer, (uint32_t)word_count);
        }
    }

    return crc;
}
