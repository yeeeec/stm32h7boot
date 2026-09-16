#include "platform/platform_storage.h"

firmware_status_t PlatformStorage_Init(void)
{
    return StorageSd_Init();
}
firmware_status_t PlatformStorage_Mount(void)
{
    return StorageSd_Mount();
}
firmware_status_t PlatformStorage_Unmount(void)
{
    return StorageSd_Unmount();
}
firmware_status_t PlatformStorage_Open(const char *path, storage_open_mode_t mode, uint32_t *handle)
{
    return StorageSd_Open(path, mode, handle);
}
firmware_status_t PlatformStorage_Read(uint32_t handle, uint32_t offset, void *buffer, size_t size,
                                       size_t *bytes_read)
{
    return StorageSd_Read(handle, offset, buffer, size, bytes_read);
}
firmware_status_t PlatformStorage_Write(uint32_t handle, const void *buffer, size_t size,
                                        size_t *bytes_written)
{
    return StorageSd_Write(handle, buffer, size, bytes_written);
}
firmware_status_t PlatformStorage_Sync(uint32_t handle)
{
    return StorageSd_Sync(handle);
}
firmware_status_t PlatformStorage_Close(uint32_t handle)
{
    return StorageSd_Close(handle);
}
firmware_status_t PlatformStorage_Stat(const char *path, storage_file_info_t *info)
{
    return StorageSd_Stat(path, info);
}
firmware_status_t PlatformStorage_Mkdir(const char *path)
{
    return StorageSd_Mkdir(path);
}
firmware_status_t PlatformStorage_Remove(const char *path)
{
    return StorageSd_Unlink(path);
}
firmware_status_t PlatformStorage_Rename(const char *old_path, const char *new_path)
{
    return StorageSd_Rename(old_path, new_path);
}
