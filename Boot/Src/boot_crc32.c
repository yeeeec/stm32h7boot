#include "boot_crc32.h"

uint32_t Boot_Crc32_Calc(const void *data, size_t length, uint32_t seed)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = ~seed;

  if (bytes == NULL)
  {
    return 0U;
  }

  for (size_t i = 0; i < length; ++i)
  {
    crc ^= bytes[i];
    for (uint32_t bit = 0; bit < 8U; ++bit)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}
