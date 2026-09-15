#ifndef FIRMWARE_STORAGE_COMMON_H
#define FIRMWARE_STORAGE_COMMON_H

#include <stddef.h>

#include "ports/storage_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

int Storage_IsSafePath(const char *path);
size_t Storage_FormatBootUpdateRequest(const storage_boot_update_request_t *request,
                                       char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
