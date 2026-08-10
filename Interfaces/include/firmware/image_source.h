/**
 * @file image_source.h
 * @brief 面向外部 MCU 镜像安装器的只读随机访问接口。
 *
 * Source 可以来自 FatFs、eMMC、网络缓存或 Host 测试。接口不暴露文件名、
 * 文件系统和缓存所有权；调用者持有 data 缓冲区，Provider 必须在 read() 返回
 * 前填满请求的全部字节，不能以“部分成功”掩盖短读。
 */
#ifndef FIRMWARE_IMAGE_SOURCE_H
#define FIRMWARE_IMAGE_SOURCE_H

#include <stdint.h>

#include "firmware/status.h"

/** 镜像 Source 的不可变属性。 */
typedef struct
{
    /** 镜像精确字节数；不得为零。 */
    uint32_t size_bytes;
} firmware_image_info_t;

/** 查询镜像尺寸；接口绑定期间结果必须保持稳定。 */
typedef firmware_status_t (*firmware_image_source_get_info_fn)(
    void *context,
    firmware_image_info_t *info);

/** 从镜像的字节偏移读取一个有界范围。 */
typedef firmware_status_t (*firmware_image_source_read_fn)(
    void *context,
    uint32_t offset,
    uint8_t *data,
    uint32_t size);

/**
 * @brief 外部 MCU 固件镜像的只读访问 Contract。
 *
 * read() 不允许跨越 info.size_bytes；Provider 不应保留 data 指针，Service
 * 可能在下一次 Process 调用中复用同一个缓冲区。
 */
typedef struct
{
    void *context;
    firmware_image_source_get_info_fn get_info;
    firmware_image_source_read_fn read;
} firmware_image_source_t;

#endif
