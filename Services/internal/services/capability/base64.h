/**
 * @file base64.h
 * @brief 供 Manifest 等受信任文档使用的严格 RFC 4648 Base64 解码接口。
 *
 * 接口只接受基础 Base64 字母表及规范填充形式，不接受空白、换行或 URL-safe
 * 变体，确保上层对同一二进制字段只有唯一的文本表示。
 */
#ifndef SERVICES_BASE64_H
#define SERVICES_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/**
 * @brief 将完整且规范的 RFC 4648 Base64 文本解码到调用者提供的缓冲区。
 *
 * @param encoded 输入 Base64 文本，不能为 NULL。
 * @param encoded_size 输入文本长度，必须非零且为 4 的整数倍。
 * @param decoded 输出二进制缓冲区，不能为 NULL。
 * @param decoded_capacity @p decoded 的总容量，单位为字节。
 * @param decoded_size 成功时接收实际写入字节数的地址，不能为 NULL。
 *
 * @return FIRMWARE_STATUS_OK 表示成功；FIRMWARE_STATUS_INVALID_ARGUMENT 表示指针
 *         参数无效；FIRMWARE_STATUS_INVALID_STATE 表示编码格式、填充位置或未使用
 *         位不符合规范；FIRMWARE_STATUS_OUT_OF_RANGE 表示输出容量不足。
 *
 * 失败时不会产生可供上层信任的输出长度；调用者应仅在返回成功后读取
 * @p decoded 和 @p decoded_size。
 */
firmware_status_t Base64_DecodeStrict(
    const char *encoded,
    size_t encoded_size,
    uint8_t *decoded,
    size_t decoded_capacity,
    size_t *decoded_size);

#endif /* SERVICES_BASE64_H：防止该内部接口被重复包含。 */
