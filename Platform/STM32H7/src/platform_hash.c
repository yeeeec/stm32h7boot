#include "platform/platform_hash.h"

firmware_status_t PlatformHash_Init(platform_hash_context_t *context)
{
    return Crypto_Sha256Init(context);
}
firmware_status_t PlatformHash_Update(platform_hash_context_t *context, const void *data,
                                      size_t size)
{
    return Crypto_Sha256Update(context, (const uint8_t *) data, size);
}
firmware_status_t PlatformHash_Final(platform_hash_context_t *context, uint8_t digest[32])
{
    return Crypto_Sha256Finish(context, digest);
}
