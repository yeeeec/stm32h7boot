#include "checksum/crc8.h"

uint8_t Checksum_Crc8(const uint8_t *data, uint32_t size)
{
    uint8_t crc = 0xFFU;
    if (data == 0)
    {
        return 0U;
    }
    while (size-- != 0U)
    {
        crc ^= *data++;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 0x80U) != 0U ? (uint8_t) ((crc << 1U) ^ 0x1DU) : (uint8_t) (crc << 1U);
        }
    }
    return crc;
}
