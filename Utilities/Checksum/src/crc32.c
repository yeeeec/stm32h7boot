#include "checksum/crc32.h"

uint32_t Checksum_Crc32IeeeBegin(void)
{
    return 0xFFFFFFFFUL;
}

uint32_t Checksum_Crc32IeeeUpdate(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t index;

    if ((data == NULL) && (size != 0U))
    {
        return crc;
    }
    for (index = 0U; index < size; ++index)
    {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 1U) != 0U) ? (crc >> 1U) ^ 0xEDB88320UL : (crc >> 1U);
        }
    }
    return crc;
}

uint32_t Checksum_Crc32IeeeFinish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t Checksum_Crc32Ieee(const uint8_t *data, size_t size)
{
    return Checksum_Crc32IeeeFinish(
        Checksum_Crc32IeeeUpdate(Checksum_Crc32IeeeBegin(), data, size));
}
