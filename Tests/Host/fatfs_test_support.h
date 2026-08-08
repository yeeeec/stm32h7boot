#ifndef FATFS_TEST_SUPPORT_H
#define FATFS_TEST_SUPPORT_H

#include <stdint.h>

#include "ff.h"

typedef struct
{
    FRESULT mount_result;
    FRESULT unmount_result;
    FRESULT open_result;
    FRESULT close_result;
    FRESULT seek_result;
    FRESULT read_result;
    FRESULT unlink_result;
    FRESULT stat_result;
    uint8_t media_present;
    FSIZE_t file_size;
    const uint8_t *file_data;
    UINT read_limit;
    int use_read_limit;
    uint32_t mount_calls;
    uint32_t open_calls;
    uint32_t close_calls;
    uint32_t read_calls;
    uint32_t seek_calls;
    uint32_t unlink_calls;
    char last_path[320];
} fatfs_test_state_t;

extern fatfs_test_state_t fatfs_test_state;

void FatFsTest_Reset(void);

#endif
