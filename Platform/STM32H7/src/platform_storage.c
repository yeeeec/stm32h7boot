#include "platform/platform_storage.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

#include "fatfs.h"
#include "ff.h"
#include "sdmmc.h"

#define PLATFORM_STORAGE_FILE_COUNT 4U
#define PLATFORM_STORAGE_DIR_COUNT  2U

extern char SDPath[4];
extern FATFS SDFatFS;

typedef struct
{
    FIL file;
    uint8_t used;
} file_slot_t;

typedef struct
{
    DIR directory;
    uint8_t used;
} dir_slot_t;

static file_slot_t s_files[PLATFORM_STORAGE_FILE_COUNT];
static dir_slot_t s_directories[PLATFORM_STORAGE_DIR_COUNT];
static uint8_t s_initialized;
static uint8_t s_mounted;

static firmware_status_t MapFatFsStatus(FRESULT result)
{
    switch (result)
    {
        case FR_OK:
            return FIRMWARE_STATUS_OK;

        case FR_NO_FILE:
        case FR_NO_PATH:
            return FIRMWARE_STATUS_NOT_FOUND;

        case FR_TIMEOUT:
        case FR_LOCKED:
        case FR_TOO_MANY_OPEN_FILES:
            return FIRMWARE_STATUS_BUSY;

        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;

        case FR_INVALID_OBJECT:
        case FR_NOT_ENABLED:
            return FIRMWARE_STATUS_INVALID_STATE;

        case FR_WRITE_PROTECTED:
        case FR_DENIED:
            return FIRMWARE_STATUS_NOT_SUPPORTED;

        case FR_DISK_ERR:
        case FR_INT_ERR:
        case FR_NOT_READY:
        case FR_EXIST:
        case FR_INVALID_DRIVE:
        case FR_NO_FILESYSTEM:
        case FR_MKFS_ABORTED:
        case FR_NOT_ENOUGH_CORE:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static file_slot_t *GetFileSlot(platform_file_handle_t handle)
{
    uint32_t index;

    if (handle == 0U)
    {
        return NULL;
    }
    index = handle - 1U;
    if ((index >= PLATFORM_STORAGE_FILE_COUNT) || (s_files[index].used == 0U))
    {
        return NULL;
    }
    return &s_files[index];
}

static dir_slot_t *GetDirSlot(platform_dir_handle_t handle)
{
    uint32_t index;

    if (handle == 0U)
    {
        return NULL;
    }
    index = handle - 1U;
    if ((index >= PLATFORM_STORAGE_DIR_COUNT) || (s_directories[index].used == 0U))
    {
        return NULL;
    }
    return &s_directories[index];
}

firmware_status_t PlatformStorage_Init(void)
{
    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    MX_SDMMC1_SD_Init();
    MX_FATFS_Init();
    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformStorage_Mount(void)
{
    FRESULT result;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (s_mounted != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    result = f_mount(&SDFatFS, SDPath, 1U);
    if (result == FR_OK)
    {
        s_mounted = 1U;
    }
    return MapFatFsStatus(result);
}

firmware_status_t PlatformStorage_Unmount(void)
{
    firmware_status_t first_status = FIRMWARE_STATUS_OK;
    uint32_t i;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    for (i = 0U; i < PLATFORM_STORAGE_FILE_COUNT; ++i)
    {
        if (s_files[i].used != 0U)
        {
            firmware_status_t status = MapFatFsStatus(f_close(&s_files[i].file));
            if ((first_status == FIRMWARE_STATUS_OK) && (status != FIRMWARE_STATUS_OK))
            {
                first_status = status;
            }
            s_files[i].used = 0U;
        }
    }

    for (i = 0U; i < PLATFORM_STORAGE_DIR_COUNT; ++i)
    {
        if (s_directories[i].used != 0U)
        {
            firmware_status_t status = MapFatFsStatus(f_closedir(&s_directories[i].directory));
            if ((first_status == FIRMWARE_STATUS_OK) && (status != FIRMWARE_STATUS_OK))
            {
                first_status = status;
            }
            s_directories[i].used = 0U;
        }
    }

    if (s_mounted != 0U)
    {
        firmware_status_t status = MapFatFsStatus(f_mount(NULL, SDPath, 1U));
        if ((first_status == FIRMWARE_STATUS_OK) && (status != FIRMWARE_STATUS_OK))
        {
            first_status = status;
        }
        s_mounted = 0U;
    }

    return first_status;
}

firmware_status_t PlatformStorage_OpenRead(const char *path, platform_file_handle_t *handle)
{
    uint32_t i;
    FRESULT result;

    if ((path == NULL) || (handle == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_mounted == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    for (i = 0U; i < PLATFORM_STORAGE_FILE_COUNT; ++i)
    {
        if (s_files[i].used == 0U)
        {
            result = f_open(&s_files[i].file, path, FA_READ);
            if (result != FR_OK)
            {
                return MapFatFsStatus(result);
            }
            s_files[i].used = 1U;
            *handle         = i + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }

    return FIRMWARE_STATUS_BUSY;
}

firmware_status_t PlatformStorage_OpenWrite(const char *path, platform_file_handle_t *handle)
{
    uint32_t i;
    FRESULT result;

    if ((path == NULL) || (handle == NULL))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (s_mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    for (i = 0U; i < PLATFORM_STORAGE_FILE_COUNT; ++i)
    {
        if (s_files[i].used == 0U)
        {
            result = f_open(&s_files[i].file, path, FA_WRITE | FA_CREATE_ALWAYS);
            if (result != FR_OK)
                return MapFatFsStatus(result);
            s_files[i].used = 1U;
            *handle         = i + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }
    return FIRMWARE_STATUS_BUSY;
}

firmware_status_t PlatformStorage_Read(platform_file_handle_t handle, void *buffer, size_t size,
                                       size_t *bytes_read)
{
    file_slot_t *slot = GetFileSlot(handle);
    UINT request_size;
    UINT actual_size = 0U;
    FRESULT result;

    if ((slot == NULL) || (buffer == NULL) || (bytes_read == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    request_size = (size > (size_t) UINT_MAX) ? UINT_MAX : (UINT) size;
    result       = f_read(&slot->file, buffer, request_size, &actual_size);
    *bytes_read  = (size_t) actual_size;
    return MapFatFsStatus(result);
}

firmware_status_t PlatformStorage_Seek(platform_file_handle_t handle, uint32_t offset)
{
    file_slot_t *slot = GetFileSlot(handle);
    if (slot == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return MapFatFsStatus(f_lseek(&slot->file, (FSIZE_t) offset));
}

firmware_status_t PlatformStorage_Write(platform_file_handle_t handle, const void *buffer,
                                        size_t size, size_t *bytes_written)
{
    file_slot_t *slot = GetFileSlot(handle);
    UINT request_size;
    UINT actual_size = 0U;
    FRESULT result;

    if ((slot == NULL) || (buffer == NULL) || (bytes_written == NULL) || (size == 0U))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    request_size   = (size > (size_t) UINT_MAX) ? UINT_MAX : (UINT) size;
    result         = f_write(&slot->file, buffer, request_size, &actual_size);
    *bytes_written = (size_t) actual_size;
    return MapFatFsStatus(result);
}

firmware_status_t PlatformStorage_Sync(platform_file_handle_t handle)
{
    file_slot_t *slot = GetFileSlot(handle);
    if (slot == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return MapFatFsStatus(f_sync(&slot->file));
}

firmware_status_t PlatformStorage_Close(platform_file_handle_t handle)
{
    file_slot_t *slot = GetFileSlot(handle);
    firmware_status_t status;

    if (slot == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status     = MapFatFsStatus(f_close(&slot->file));
    slot->used = 0U;
    return status;
}

firmware_status_t PlatformStorage_Stat(const char *path, platform_file_info_t *info)
{
    FILINFO file_info;
    FRESULT result;

    if ((path == NULL) || (info == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_mounted == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    result = f_stat(path, &file_info);
    if (result != FR_OK)
    {
        return MapFatFsStatus(result);
    }
    if (file_info.fsize > UINT32_MAX)
    {
        return FIRMWARE_STATUS_OVERFLOW;
    }

    info->size         = (uint32_t) file_info.fsize;
    info->is_directory = ((file_info.fattrib & AM_DIR) != 0U) ? 1U : 0U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformStorage_Mkdir(const char *path)
{
    if ((path == NULL) || (s_mounted == 0U))
        return (path == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT : FIRMWARE_STATUS_INVALID_STATE;
    return MapFatFsStatus(f_mkdir(path));
}

firmware_status_t PlatformStorage_Remove(const char *path)
{
    if ((path == NULL) || (s_mounted == 0U))
        return (path == NULL) ? FIRMWARE_STATUS_INVALID_ARGUMENT : FIRMWARE_STATUS_INVALID_STATE;
    return MapFatFsStatus(f_unlink(path));
}

firmware_status_t PlatformStorage_Rename(const char *old_path, const char *new_path)
{
    if ((old_path == NULL) || (new_path == NULL))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (s_mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    return MapFatFsStatus(f_rename(old_path, new_path));
}

firmware_status_t PlatformStorage_RemoveTree(const char *path)
{
    platform_dir_handle_t directory;
    platform_dir_entry_t entry;
    char child[PLATFORM_STORAGE_NAME_MAX + 96U];
    firmware_status_t status;

    if (path == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = PlatformStorage_DirOpen(path, &directory);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return FIRMWARE_STATUS_OK;
    if (FirmwareStatus_IsError(status))
        return status;
    for (;;)
    {
        status = PlatformStorage_DirRead(directory, &entry);
        if (status == FIRMWARE_STATUS_NOT_FOUND)
            break;
        if (FirmwareStatus_IsError(status))
        {
            (void) PlatformStorage_DirClose(directory);
            return status;
        }
        if ((entry.name[0] == '.') &&
            ((entry.name[1] == '\0') || ((entry.name[1] == '.') && (entry.name[2] == '\0'))))
            continue;
        {
            int length = snprintf(child, sizeof(child), "%s/%s", path, entry.name);
            if (length <= 0 || (size_t) length >= sizeof(child))
            {
                (void) PlatformStorage_DirClose(directory);
                return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
            }
        }
        status =
            entry.is_directory ? PlatformStorage_RemoveTree(child) : PlatformStorage_Remove(child);
        if (FirmwareStatus_IsError(status))
        {
            (void) PlatformStorage_DirClose(directory);
            return status;
        }
    }
    status = PlatformStorage_DirClose(directory);
    if (FirmwareStatus_IsError(status))
        return status;
    return PlatformStorage_Remove(path);
}

firmware_status_t PlatformStorage_DirOpen(const char *path, platform_dir_handle_t *handle)
{
    uint32_t i;
    FRESULT result;

    if ((path == NULL) || (handle == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_mounted == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    for (i = 0U; i < PLATFORM_STORAGE_DIR_COUNT; ++i)
    {
        if (s_directories[i].used == 0U)
        {
            result = f_opendir(&s_directories[i].directory, path);
            if (result != FR_OK)
            {
                return MapFatFsStatus(result);
            }
            s_directories[i].used = 1U;
            *handle               = i + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }

    return FIRMWARE_STATUS_BUSY;
}

firmware_status_t PlatformStorage_DirRead(platform_dir_handle_t handle, platform_dir_entry_t *entry)
{
    dir_slot_t *slot = GetDirSlot(handle);
    FILINFO file_info;
    FRESULT result;
    size_t i;

    if ((slot == NULL) || (entry == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    result = f_readdir(&slot->directory, &file_info);
    if (result != FR_OK)
    {
        return MapFatFsStatus(result);
    }
    if (file_info.fname[0] == '\0')
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    if (file_info.fsize > UINT32_MAX)
    {
        return FIRMWARE_STATUS_OVERFLOW;
    }

    for (i = 0U; (i + 1U) < PLATFORM_STORAGE_NAME_MAX; ++i)
    {
        if (file_info.fname[i] == '\0')
        {
            break;
        }
        entry->name[i] = file_info.fname[i];
    }
    if (file_info.fname[i] != '\0')
    {
        entry->name[PLATFORM_STORAGE_NAME_MAX - 1U] = '\0';
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    entry->name[i] = '\0';

    entry->size         = (uint32_t) file_info.fsize;
    entry->is_directory = ((file_info.fattrib & AM_DIR) != 0U) ? 1U : 0U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformStorage_DirClose(platform_dir_handle_t handle)
{
    dir_slot_t *slot = GetDirSlot(handle);
    firmware_status_t status;

    if (slot == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status     = MapFatFsStatus(f_closedir(&slot->directory));
    slot->used = 0U;
    return status;
}
