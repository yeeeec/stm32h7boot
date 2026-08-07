/**
 * @file package_source.h
 * @brief Fixed release-package read-only source contract.
 */
#ifndef FIRMWARE_PACKAGE_SOURCE_H
#define FIRMWARE_PACKAGE_SOURCE_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    PACKAGE_FILE_MANIFEST = 0,
    PACKAGE_FILE_APP,
    PACKAGE_FILE_GUI
} package_file_id_t;

typedef firmware_status_t (*package_source_is_media_present_fn)(
    void *context,
    int *present);
typedef firmware_status_t (*package_source_mount_fn)(void *context);
typedef firmware_status_t (*package_source_unmount_fn)(void *context);
typedef firmware_status_t (*package_source_open_fn)(
    void *context,
    package_file_id_t file);
typedef firmware_status_t (*package_source_close_fn)(void *context);
typedef firmware_status_t (*package_source_get_size_fn)(
    void *context,
    uint32_t *size);
typedef firmware_status_t (*package_source_read_at_fn)(
    void *context,
    uint32_t offset,
    uint8_t *data,
    uint32_t size,
    uint32_t *bytes_read);

/**
 * Read-only access to the three fixed release files on one mounted volume.
 * The interface intentionally has no pathname, exists, or remove operation.
 */
typedef struct
{
    void *context;
    package_source_is_media_present_fn is_media_present;
    package_source_mount_fn mount;
    package_source_unmount_fn unmount;
    package_source_open_fn open;
    package_source_close_fn close;
    package_source_get_size_fn get_size;
    package_source_read_at_fn read_at;
} package_source_t;

#endif
