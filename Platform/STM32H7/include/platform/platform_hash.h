#ifndef PLATFORM_HASH_H
#define PLATFORM_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "crypto/sha256.h"
#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef crypto_sha256_context_t platform_hash_context_t;
    firmware_status_t PlatformHash_Init(platform_hash_context_t *context);
    firmware_status_t PlatformHash_Update(platform_hash_context_t *context, const void *data,
                                          size_t size);
    firmware_status_t PlatformHash_Final(platform_hash_context_t *context, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif
