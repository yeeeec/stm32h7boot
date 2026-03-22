#ifndef BOOT_SIMPLE_MANIFEST_H
#define BOOT_SIMPLE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "boot_config.h"
#include "boot_error.h"

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  uint32_t size;
  uint32_t crc32;
  uint32_t version;
  uint8_t present_in_crc_dir;
} BootManifestOperation;

typedef struct
{
  BootManifestOperation operations[BOOT_MANIFEST_MAX_OPERATIONS];
  uint32_t count;
} BootManifest;

void Boot_SimpleManifest_Reset(BootManifest *manifest);
BootError Boot_SimpleManifest_Load(BootManifest *manifest);
const BootManifestOperation *Boot_SimpleManifest_FindFirstSupported(const BootManifest *manifest);
BootError Boot_SimpleManifest_BuildCrcPath(const char *file_name, char *buffer, size_t buffer_length);

#endif
