/**
 * @file platform_cache.c
 * @brief 在 DMA Ownership 边界执行带校验的 CMSIS D-Cache Maintenance。
 *
 * 必须使用精确的 Cache Line 范围，避免 Invalidate 丢弃相邻对象的脏字节。
 * Wrapper 还保护 CMSIS 的有符号长度 ABI，避免 size_t 截断。
 */
#include "platform/platform_cache.h"

#include <stdint.h>

#include "stm32h7xx.h"

static firmware_status_t CacheValidate(const void *address, size_t size)
{
    /* CMSIS 使用有符号字节数；拒绝其无法表示的值。 */
    if ((address == NULL) || (size == 0U) || (size > (size_t) INT32_MAX) ||
        (((uintptr_t) address % PLATFORM_DCACHE_LINE_SIZE) != 0U) ||
        ((size % PLATFORM_DCACHE_LINE_SIZE) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_DCacheClean(const void *address, size_t size)
{
    firmware_status_t status = CacheValidate(address, size);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    SCB_CleanDCache_by_Addr((uint32_t *) address, (int32_t) size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_DCacheInvalidate(const void *address, size_t size)
{
    firmware_status_t status = CacheValidate(address, size);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *) address, (int32_t) size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_DCacheCleanInvalidate(const void *address, size_t size)
{
    firmware_status_t status = CacheValidate(address, size);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *) address, (int32_t) size);
    return FIRMWARE_STATUS_OK;
}
