#ifndef FIRMWARE_MBEDTLS_CONFIG_H
#define FIRMWARE_MBEDTLS_CONFIG_H

/* Lily HMI requires only SHA-256 and ECDSA P-256 signature verification. */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_SHA256_C

#include "mbedtls/check_config.h"

#endif
