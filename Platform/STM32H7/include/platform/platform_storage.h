#ifndef PLATFORM_STORAGE_H
#define PLATFORM_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "ports/storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Bootloader-facing SD facade.  FatFs types remain private to Platform. */
    firmware_status_t PlatformStorage_Init(void);
    firmware_status_t PlatformStorage_Mount(void);
    firmware_status_t PlatformStorage_Unmount(void);
    firmware_status_t PlatformStorage_Open(const char *path, storage_open_mode_t mode,
                                           uint32_t *handle);
    firmware_status_t PlatformStorage_Read(uint32_t handle, uint32_t offset, void *buffer,
                                           size_t size, size_t *bytes_read);
    firmware_status_t PlatformStorage_Write(uint32_t handle, const void *buffer, size_t size,
                                            size_t *bytes_written);
    firmware_status_t PlatformStorage_Sync(uint32_t handle);
    firmware_status_t PlatformStorage_Close(uint32_t handle);
    firmware_status_t PlatformStorage_Stat(const char *path, storage_file_info_t *info);
    firmware_status_t PlatformStorage_Mkdir(const char *path);
    firmware_status_t PlatformStorage_Remove(const char *path);
    firmware_status_t PlatformStorage_Rename(const char *old_path, const char *new_path);

#ifdef __cplusplus
}
#endif

#endif
