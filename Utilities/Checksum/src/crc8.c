#include "checksum/crc8.h"

#include <stddef.h>

uint8_t Checksum_Crc8(const uint8_t *data, uint32_t size)
{
    uint8_t crc = 0xFFU;

    /* Preserve the STM32H7APP API convention: NULL is invalid even for an
     * empty input and is represented by the sentinel value zero. */
    if (data == NULL)
        return 0U;
    while (size-- != 0U)
    {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x80U) != 0U ? (uint8_t) ((crc << 1U) ^ 0x1DU)
                                      : (uint8_t) (crc << 1U);
    }
    return crc;
}
