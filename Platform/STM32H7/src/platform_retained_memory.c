#include "platform/platform_retained_memory.h"

#include <string.h>

#include "main.h"

#if defined(__GNUC__)
static unsigned char s_retained_memory[PLATFORM_RETAINED_MEMORY_SIZE]
    __attribute__((section(".noinit"), used, aligned(4)));
#else
static unsigned char s_retained_memory[PLATFORM_RETAINED_MEMORY_SIZE];
#endif

firmware_status_t PlatformRetainedMemory_Read(void *data, size_t size)
{
    if ((data == NULL) || (size == 0U) || (size > PLATFORM_RETAINED_MEMORY_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    (void) memcpy(data, s_retained_memory, size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformRetainedMemory_Write(const void *data, size_t size)
{
    if ((data == NULL) || (size == 0U) || (size > PLATFORM_RETAINED_MEMORY_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    (void) memcpy(s_retained_memory, data, size);
    __DSB();
    return FIRMWARE_STATUS_OK;
}

void PlatformRetainedMemory_Clear(void)
{
    (void) memset(s_retained_memory, 0, sizeof(s_retained_memory));
    __DSB();
}
