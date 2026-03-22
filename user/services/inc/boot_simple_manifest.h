#ifndef BOOT_SIMPLE_MANIFEST_H
#define BOOT_SIMPLE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "boot_config.h"
#include "boot_error.h"

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  char project_name[BOOT_VERSION_PROJECT_NAME_LENGTH + 1U];
  char git_hash[BOOT_VERSION_GIT_HASH_LENGTH + 1U];
  char build_time[BOOT_BUILD_TIME_LENGTH + 1U];
  uint32_t size;
  uint32_t crc32;
  uint32_t write_address;
  uint8_t header[BOOT_VERSION_INFO_SIZE];
} BootAppImageInfo;

void Boot_SimpleManifest_Reset(BootAppImageInfo *image);
BootError Boot_SimpleManifest_Load(BootAppImageInfo *image);
BootError Boot_SimpleManifest_BuildAppPath(char *buffer, size_t buffer_length);

#endif
