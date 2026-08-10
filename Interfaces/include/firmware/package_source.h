/**
 * @file package_source.h
 * @brief 固定 Release Package 的只读 Source Contract。
 */
#ifndef FIRMWARE_PACKAGE_SOURCE_H
#define FIRMWARE_PACKAGE_SOURCE_H

#include <stdint.h>

#include "firmware/status.h"

/** Release Package Contract 定义的固定文件标识。 */
typedef enum
{
    PACKAGE_FILE_MANIFEST = 0,
    PACKAGE_FILE_APP,
    PACKAGE_FILE_GUI,
    /** Optional third-party MCU image reserved for serial programming. */
    PACKAGE_FILE_THERAPY_APP
} package_file_id_t;

/** 查询介质是否存在，不取得已打开文件的 Ownership。 */
typedef firmware_status_t (*package_source_is_media_present_fn)(
    void *context,
    int *present);
/** 挂载尚未挂载的 Package Volume；重复挂载可能被拒绝。 */
typedef firmware_status_t (*package_source_mount_fn)(void *context);
/** 所有已打开文件关闭后卸载 Volume。 */
typedef firmware_status_t (*package_source_unmount_fn)(void *context);
/** 打开一个固定文件；成功后文件 Ownership 转移给调用者。 */
typedef firmware_status_t (*package_source_open_fn)(
    void *context,
    package_file_id_t file);
/** 关闭当前文件；只有 close 成功后 Ownership 才结束。 */
typedef firmware_status_t (*package_source_close_fn)(void *context);
/** 返回当前已打开固定文件的大小。 */
typedef firmware_status_t (*package_source_get_size_fn)(
    void *context,
    uint32_t *size);
/** 从当前已打开文件读取有界范围。 */
typedef firmware_status_t (*package_source_read_at_fn)(
    void *context,
    uint32_t offset,
    uint8_t *data,
    uint32_t size,
    uint32_t *bytes_read);

/**
 * @brief 对一个 Volume 中固定 Release 文件的只读访问。
 *
 * 该接口有意不提供 pathname、exists 或 remove 操作。Provider 持有已挂载
 * Volume；调用者负责当前文件的 Open/Close 生命周期，并必须在卸载前成功 close。
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
