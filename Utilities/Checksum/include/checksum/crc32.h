#ifndef CHECKSUM_CRC32_H
#define CHECKSUM_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* CRC-32/ISO-HDLC, matching STM32H7APP and the boot-control format. */
uint32_t Checksum_Crc32IeeeBegin(void);
uint32_t Checksum_Crc32IeeeUpdate(uint32_t crc, const uint8_t *data, size_t size);
uint32_t Checksum_Crc32IeeeFinish(uint32_t crc);
uint32_t Checksum_Crc32Ieee(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
