#ifndef TEST_FAKE_FF_H
#define TEST_FAKE_FF_H

#include <stdint.h>

#define _MAX_LFN 255U

typedef uint32_t FSIZE_t;
typedef unsigned int UINT;
typedef unsigned char BYTE;

typedef struct
{
    uint32_t unused;
} FATFS;

typedef struct
{
    FSIZE_t size;
    FSIZE_t position;
    int open;
} FIL;

typedef struct
{
    uint32_t unused;
} FILINFO;

typedef enum
{
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ 0x01U
#define FA_OPEN_EXISTING 0x00U

FRESULT f_mount(FATFS *fs, const char *path, BYTE opt);
FRESULT f_open(FIL *file, const char *path, BYTE mode);
FRESULT f_close(FIL *file);
FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *bytes_read);
FRESULT f_lseek(FIL *file, FSIZE_t offset);
FRESULT f_unlink(const char *path);
FRESULT f_stat(const char *path, FILINFO *file_info);
FSIZE_t f_size(const FIL *file);

#endif
