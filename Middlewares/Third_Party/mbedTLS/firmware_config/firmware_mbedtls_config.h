#ifndef FIRMWARE_MBEDTLS_CONFIG_H
#define FIRMWARE_MBEDTLS_CONFIG_H

/* Bootloader integrity checks require SHA-256 only. */
#define MBEDTLS_SHA256_C

#include "mbedtls/check_config.h"

#endif
