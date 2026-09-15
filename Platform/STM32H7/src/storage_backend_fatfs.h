#ifndef PLATFORM_STORAGE_BACKEND_FATFS_H
#define PLATFORM_STORAGE_BACKEND_FATFS_H

#include <stdint.h>

#include "ff.h"
#include "ports/storage_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct fatfs_backend fatfs_backend_t;
    typedef firmware_status_t (*fatfs_prepare_mount_fn)(void *context);

    typedef struct
    {
        const char *volumePath;
        FATFS *fileSystem;
        fatfs_prepare_mount_fn prepareMount;
        void *context;
        uint8_t writable;
    } fatfs_backend_config_t;

    struct fatfs_backend
    {
        fatfs_backend_config_t config;
        FIL files[STORAGE_MAX_OPEN_FILES];
        DIR directories[STORAGE_MAX_OPEN_DIRECTORIES];
        uint8_t fileUsed[STORAGE_MAX_OPEN_FILES];
        uint8_t directoryUsed[STORAGE_MAX_OPEN_DIRECTORIES];
        uint8_t mounted;
    };

    void FatFsBackend_Configure(fatfs_backend_t *backend, const fatfs_backend_config_t *config);
    firmware_status_t FatFsBackend_Init(fatfs_backend_t *backend);
    firmware_status_t FatFsBackend_Mount(fatfs_backend_t *backend);
    firmware_status_t FatFsBackend_Unmount(fatfs_backend_t *backend);
    firmware_status_t FatFsBackend_Mkdir(fatfs_backend_t *backend, const char *path);
    firmware_status_t FatFsBackend_Open(fatfs_backend_t *backend, const char *path,
                                        storage_open_mode_t mode, uint32_t *handle);
    firmware_status_t FatFsBackend_Read(fatfs_backend_t *backend, uint32_t handle, uint32_t offset,
                                        void *buffer, size_t size, size_t *bytesRead);
    firmware_status_t FatFsBackend_Write(fatfs_backend_t *backend, uint32_t handle,
                                         const void *buffer, size_t size, size_t *bytesWritten);
    firmware_status_t FatFsBackend_Sync(fatfs_backend_t *backend, uint32_t handle);
    firmware_status_t FatFsBackend_Close(fatfs_backend_t *backend, uint32_t handle);
    firmware_status_t FatFsBackend_Stat(fatfs_backend_t *backend, const char *path,
                                        storage_file_info_t *info);
    firmware_status_t FatFsBackend_DirOpen(fatfs_backend_t *backend, const char *path,
                                           uint32_t *handle);
    firmware_status_t FatFsBackend_DirRead(fatfs_backend_t *backend, uint32_t handle,
                                           storage_dir_entry_t *entry);
    firmware_status_t FatFsBackend_DirClose(fatfs_backend_t *backend, uint32_t handle);
    firmware_status_t FatFsBackend_Rename(fatfs_backend_t *backend, const char *oldPath,
                                          const char *newPath);
    firmware_status_t FatFsBackend_Unlink(fatfs_backend_t *backend, const char *path);

#ifdef __cplusplus
}
#endif

#endif
