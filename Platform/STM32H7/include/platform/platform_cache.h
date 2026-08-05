#ifndef PLATFORM_CACHE_H
#define PLATFORM_CACHE_H

#include <stddef.h>

#include "firmware/status.h"

#define PLATFORM_DCACHE_LINE_SIZE 32U

/* DMA buffers passed here must have cache-line-aligned address and size. */
firmware_status_t Platform_DCacheClean(const void *address, size_t size);
firmware_status_t Platform_DCacheInvalidate(const void *address, size_t size);
firmware_status_t Platform_DCacheCleanInvalidate(const void *address, size_t size);

#endif
