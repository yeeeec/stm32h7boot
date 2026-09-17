#ifndef PLATFORM_FLASH_H
#define PLATFORM_FLASH_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PLATFORM_FLASH_CAPACITY_BYTES (32UL * 1024UL * 1024UL)
#define PLATFORM_FLASH_PAGE_SIZE      256U
#define PLATFORM_FLASH_ERASE_SIZE     4096U
#define PLATFORM_FLASH_MAPPED_BASE    0x90000000UL

    typedef struct
    {
        uint32_t capacity_bytes;
        uint32_t page_size;
        uint32_t erase_size;
        uint8_t jedec_id[3];
    } platform_flash_info_t;

    firmware_status_t PlatformFlash_Init(void);
    firmware_status_t PlatformFlash_GetInfo(platform_flash_info_t *info);

    firmware_status_t PlatformFlash_Read(uint32_t address, void *buffer, uint32_t size);

    firmware_status_t PlatformFlash_Write(uint32_t address, const void *buffer, uint32_t size);

    firmware_status_t PlatformFlash_Erase(uint32_t address, uint32_t size);

    firmware_status_t PlatformFlash_EnterMemoryMapped(void);
    firmware_status_t PlatformFlash_ExitMemoryMapped(void);
    int PlatformFlash_IsMemoryMapped(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_FLASH_H */
