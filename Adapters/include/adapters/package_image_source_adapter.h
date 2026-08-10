/**
 * @file package_image_source_adapter.h
 * @brief Adapt an already-open package file to firmware_image_source_t.
 *
 * The adapter deliberately does not open or close files.  The owner of the
 * package_source_t controls that lifecycle, which lets the same volume remain
 * shared by the normal update workflow and an optional secondary-MCU update.
 */
#ifndef ADAPTERS_PACKAGE_IMAGE_SOURCE_ADAPTER_H
#define ADAPTERS_PACKAGE_IMAGE_SOURCE_ADAPTER_H

#include "firmware/image_source.h"
#include "firmware/package_source.h"

typedef struct
{
    firmware_image_source_t interface;
    const package_source_t *package_source;
} package_image_source_adapter_t;

/**
 * @brief Bind a package source whose current file is already open.
 *
 * @param[out] adapter Static-lifetime adapter storage owned by the caller.
 * @param[in] package_source Package source implementation and current-file
 *                           lifecycle owner.
 */
firmware_status_t PackageImageSourceAdapter_Init(
    package_image_source_adapter_t *adapter,
    const package_source_t *package_source);

/** Return the image-source interface embedded in @p adapter. */
const firmware_image_source_t *PackageImageSourceAdapter_Interface(
    const package_image_source_adapter_t *adapter);

#endif
