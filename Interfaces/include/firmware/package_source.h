/**
 * @file package_source.h
 * @brief Sequential upgrade-package file source contract.
 */
#ifndef FIRMWARE_PACKAGE_SOURCE_H
#define FIRMWARE_PACKAGE_SOURCE_H

#include <stdint.h>

#include "firmware/status.h"

typedef firmware_status_t (*package_source_is_media_present_fn)(
    void *context,
    int *present);
typedef firmware_status_t (*package_source_mount_fn)(void *context);
typedef firmware_status_t (*package_source_unmount_fn)(void *context);
typedef firmware_status_t (*package_source_open_fn)(
    void *context,
    const char *path);
typedef firmware_status_t (*package_source_close_fn)(void *context);
typedef firmware_status_t (*package_source_exists_fn)(
    void *context,
    const char *path,
    int *present);
typedef firmware_status_t (*package_source_remove_fn)(
    void *context,
    const char *path);
typedef firmware_status_t (*package_source_get_size_fn)(
    void *context,
    uint32_t *size);
typedef firmware_status_t (*package_source_read_at_fn)(
    void *context,
    uint32_t offset,
    void *data,
    uint32_t size,
    uint32_t *bytes_read);

/**
 * One interface instance owns at most one open file. Paths are UTF-8,
 * null-terminated strings and remain caller-owned.
 */
typedef struct
{
    void *context; /**< Provider-owned volume and open-file state. */
    package_source_is_media_present_fn is_media_present; /**< Query removable media. */
    package_source_mount_fn mount; /**< Mount the single package volume. */
    package_source_unmount_fn unmount; /**< Unmount when no file is open. */
    package_source_open_fn open; /**< Open one caller-named file for reading. */
    package_source_close_fn close; /**< Close the currently open file. */
    package_source_exists_fn exists; /**< Test for a file without opening it. */
    package_source_remove_fn remove; /**< Remove a file while the volume is mounted. */
    package_source_get_size_fn get_size; /**< Return open-file size in bytes. */
    package_source_read_at_fn read_at; /**< Read bytes at an absolute file offset. */
} package_source_t;

#endif
