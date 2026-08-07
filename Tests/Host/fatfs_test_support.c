#include "fatfs_test_support.h"

#include <string.h>

#include "bsp_driver_sd.h"
#include "fatfs.h"

char SDPath[4] = "0:";
FATFS SDFatFS;
FIL SDFile;
fatfs_test_state_t fatfs_test_state;

void FatFsTest_Reset(void)
{
    memset(&fatfs_test_state, 0, sizeof(fatfs_test_state));
    memset(&SDFatFS, 0, sizeof(SDFatFS));
    memset(&SDFile, 0, sizeof(SDFile));
    fatfs_test_state.mount_result = FR_OK;
    fatfs_test_state.open_result = FR_OK;
    fatfs_test_state.close_result = FR_OK;
    fatfs_test_state.seek_result = FR_OK;
    fatfs_test_state.read_result = FR_OK;
    fatfs_test_state.unlink_result = FR_OK;
    fatfs_test_state.stat_result = FR_OK;
    fatfs_test_state.media_present = SD_PRESENT;
}

uint8_t BSP_SD_IsDetected(void)
{
    return fatfs_test_state.media_present;
}

FRESULT f_mount(FATFS *fs, const char *path, BYTE opt)
{
    (void)fs;
    (void)path;
    (void)opt;
    ++fatfs_test_state.mount_calls;
    return fatfs_test_state.mount_result;
}

FRESULT f_open(FIL *file, const char *path, BYTE mode)
{
    (void)mode;
    ++fatfs_test_state.open_calls;
    (void)strncpy(fatfs_test_state.last_path, path,
                  sizeof(fatfs_test_state.last_path) - 1U);
    fatfs_test_state.last_path[sizeof(fatfs_test_state.last_path) - 1U] = '\0';
    if (fatfs_test_state.open_result == FR_OK)
    {
        file->size = fatfs_test_state.file_size;
        file->position = 0U;
        file->open = 1;
    }
    return fatfs_test_state.open_result;
}

FRESULT f_close(FIL *file)
{
    ++fatfs_test_state.close_calls;
    if (fatfs_test_state.close_result == FR_OK)
    {
        file->open = 0;
    }
    return fatfs_test_state.close_result;
}

FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *bytes_read)
{
    UINT available;
    UINT count;

    ++fatfs_test_state.read_calls;
    if (fatfs_test_state.read_result != FR_OK)
    {
        *bytes_read = 0U;
        return fatfs_test_state.read_result;
    }
    available = (file->position >= file->size) ? 0U : (UINT)(file->size - file->position);
    count = (size < available) ? size : available;
    if ((fatfs_test_state.use_read_limit != 0) && (count > fatfs_test_state.read_limit))
    {
        count = fatfs_test_state.read_limit;
    }
    if ((count != 0U) && (fatfs_test_state.file_data != NULL))
    {
        memcpy(buffer, &fatfs_test_state.file_data[file->position], count);
    }
    file->position += count;
    *bytes_read = count;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset)
{
    ++fatfs_test_state.seek_calls;
    if (fatfs_test_state.seek_result == FR_OK)
    {
        file->position = offset;
    }
    return fatfs_test_state.seek_result;
}

FRESULT f_unlink(const char *path)
{
    ++fatfs_test_state.unlink_calls;
    (void)strncpy(fatfs_test_state.last_path, path,
                  sizeof(fatfs_test_state.last_path) - 1U);
    fatfs_test_state.last_path[sizeof(fatfs_test_state.last_path) - 1U] = '\0';
    return fatfs_test_state.unlink_result;
}

FRESULT f_stat(const char *path, FILINFO *file_info)
{
    (void)path;
    (void)file_info;
    return fatfs_test_state.stat_result;
}

FSIZE_t f_size(const FIL *file)
{
    return file->size;
}
