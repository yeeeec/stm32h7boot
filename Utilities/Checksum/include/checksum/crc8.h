#ifndef CHECKSUM_CRC8_H
#define CHECKSUM_CRC8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    uint8_t Checksum_Crc8(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
