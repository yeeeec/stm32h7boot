/**
 * @file fatfs_package_source_adapter.h
 * @brief SD-card FatFs adapters for the release volume.
 */
#ifndef ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H
#define ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H

#include "firmware/package_source.h"

/** Shared mount and open-file ownership for the one SD/FatFs release volume. */
typedef struct
{
    int mounted;
    int package_file_open;
} fatfs_release_volume_context_t;

typedef struct
{
    package_source_t interface;
    fatfs_release_volume_context_t *volume;
} fatfs_package_source_adapter_t;

firmware_status_t FatFsReleaseVolumeContext_Init(
    fatfs_release_volume_context_t *volume);

/**
 * Initialize fixed-file and deprecated pathname compatibility access.
 * The caller owns @p volume and must share it with the request-store adapter.
 */
firmware_status_t FatFsPackageSourceAdapter_Init(
    fatfs_package_source_adapter_t *adapter,
    fatfs_release_volume_context_t *volume);

const package_source_t *FatFsPackageSourceAdapter_Interface(
    const fatfs_package_source_adapter_t *adapter);

#endif
