#ifndef PLATFORM_STORAGE_H
#define PLATFORM_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PLATFORM_STORAGE_NAME_MAX 128U

    typedef uint32_t platform_file_handle_t;
    typedef uint32_t platform_dir_handle_t;

    typedef struct
    {
        uint32_t size;
        uint8_t is_directory;
    } platform_file_info_t;

    typedef struct
    {
        char name[PLATFORM_STORAGE_NAME_MAX];
        uint32_t size;
        uint8_t is_directory;
    } platform_dir_entry_t;

    firmware_status_t PlatformStorage_Init(void);
    firmware_status_t PlatformStorage_Mount(void);
    firmware_status_t PlatformStorage_Unmount(void);

    firmware_status_t PlatformStorage_OpenRead(const char *path, platform_file_handle_t *handle);

    firmware_status_t PlatformStorage_OpenWrite(const char *path, platform_file_handle_t *handle);

    firmware_status_t PlatformStorage_Read(platform_file_handle_t handle, void *buffer, size_t size,
                                           size_t *bytes_read);

    firmware_status_t PlatformStorage_Seek(platform_file_handle_t handle, uint32_t offset);

    firmware_status_t PlatformStorage_Write(platform_file_handle_t handle, const void *buffer,
                                            size_t size, size_t *bytes_written);

    firmware_status_t PlatformStorage_Sync(platform_file_handle_t handle);

    firmware_status_t PlatformStorage_Close(platform_file_handle_t handle);

    firmware_status_t PlatformStorage_Stat(const char *path, platform_file_info_t *info);

    firmware_status_t PlatformStorage_Mkdir(const char *path);
    firmware_status_t PlatformStorage_Remove(const char *path);
    firmware_status_t PlatformStorage_Rename(const char *old_path, const char *new_path);
    firmware_status_t PlatformStorage_RemoveTree(const char *path);

    firmware_status_t PlatformStorage_DirOpen(const char *path, platform_dir_handle_t *handle);

    firmware_status_t PlatformStorage_DirRead(platform_dir_handle_t handle,
                                              platform_dir_entry_t *entry);

    firmware_status_t PlatformStorage_DirClose(platform_dir_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_STORAGE_H */
