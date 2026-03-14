#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t Boot_Crc32_Calc(const void *data, size_t length, uint32_t seed);

#endif
