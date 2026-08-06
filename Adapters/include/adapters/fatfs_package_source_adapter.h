/**
 * @file fatfs_package_source_adapter.h
 * @brief SD-card FatFs adapter for upgrade-package file access.
 */
#ifndef ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H
#define ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H

#include "firmware/package_source.h"

/** Adapter state for the single CubeMX SD-card FatFs volume. */
typedef struct
{
    package_source_t interface;
    int mounted;
    int file_open;
} fatfs_package_source_adapter_t;

/**
 * @brief Initialize access to the CubeMX SD-card FatFs objects.
 *
 * @param[out] adapter Adapter object to initialize.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if @p adapter is NULL.
 *
 * @pre MX_FATFS_Init and MX_SDMMC1_SD_Init have completed.
 */
firmware_status_t FatFsPackageSourceAdapter_Init(
    fatfs_package_source_adapter_t *adapter);

/**
 * @brief Return the package-source interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return Adapter-owned interface, or NULL for a NULL adapter.
 */
const package_source_t *FatFsPackageSourceAdapter_Interface(
    const fatfs_package_source_adapter_t *adapter);

#endif
