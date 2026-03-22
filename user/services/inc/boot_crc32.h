#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t Boot_Crc32_Calc(const void *data, size_t length, uint32_t seed);
uint32_t Boot_Crc32_IsoUpdate(uint32_t current_crc, const void *data, size_t length);
uint32_t Boot_Crc32_IsoCalc(const void *data, size_t length);
uint32_t Boot_Crc32_Mpeg2Update(uint32_t current_crc, const void *data, size_t length);

#endif
