#ifndef FIRMWARE_STORAGE_PORT_H
#define FIRMWARE_STORAGE_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "ports/storage_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Synchronous media facade. Ownership remains with the caller/task; no RTOS
 * types are present in this contract. */
firmware_status_t StorageSd_Init(void);
firmware_status_t StorageSd_Mount(void);
firmware_status_t StorageSd_Unmount(void);
firmware_status_t StorageSd_Mkdir(const char *path);
firmware_status_t StorageSd_Open(const char *path, storage_open_mode_t mode, uint32_t *handle);
firmware_status_t StorageSd_Write(uint32_t handle, const void *buffer, size_t size,
                                  size_t *bytes_written);
firmware_status_t StorageSd_Sync(uint32_t handle);
firmware_status_t StorageSd_Close(uint32_t handle);
firmware_status_t StorageSd_Rename(const char *old_path, const char *new_path);
firmware_status_t StorageSd_Unlink(const char *path);

firmware_status_t StorageUsb_Init(void);
firmware_status_t StorageUsb_Mount(void);
firmware_status_t StorageUsb_Unmount(void);
firmware_status_t StorageUsb_OpenRead(const char *path, uint32_t *handle);
firmware_status_t StorageUsb_Read(uint32_t handle, uint32_t offset, void *buffer, size_t size,
                                  size_t *bytes_read);
firmware_status_t StorageUsb_Close(uint32_t handle);
firmware_status_t StorageUsb_Stat(const char *path, storage_file_info_t *info);
firmware_status_t StorageUsb_DirOpen(const char *path, uint32_t *handle);
firmware_status_t StorageUsb_DirRead(uint32_t handle, storage_dir_entry_t *entry);
firmware_status_t StorageUsb_DirClose(uint32_t handle);

#ifdef __cplusplus
}
#endif

#endif
