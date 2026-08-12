/**
 * @file json_document.h
 * @brief 无动态内存的严格 JSON 文档解析与规范化输出辅助接口。
 *
 * 本模块将输入 JSON 映射到调用方提供的 Token 数组中，不复制原始数据，
 * 因而适用于 Bootloader 等内存资源受限的场景。解析器只接受本固件清单
 * 所需的严格 ASCII、非负整数 JSON 子集，并拒绝对象中的重复键。
 */
#ifndef SERVICES_JSON_DOCUMENT_H
#define SERVICES_JSON_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/** 表示不存在或未选择任何 Token 的无效索引值。 */
#define JSON_DOCUMENT_NO_TOKEN UINT32_MAX

/** JSON Token 的语义类别。 */
typedef enum
{
    /** 由花括号包围的键值对象。 */
    JSON_TOKEN_OBJECT = 0,
    /** 由方括号包围且保持输入顺序的元素数组。 */
    JSON_TOKEN_ARRAY,
    /** 不含引号、未转义的 ASCII 字符串内容。 */
    JSON_TOKEN_STRING,
    /** 非负十进制整数文本。 */
    JSON_TOKEN_NUMBER,
    /** JSON 字面量 true。 */
    JSON_TOKEN_TRUE,
    /** JSON 字面量 false。 */
    JSON_TOKEN_FALSE,
    /** JSON 字面量 null。 */
    JSON_TOKEN_NULL
} json_token_type_t;

/**
 * 原始 JSON 数据中的一个语法节点。
 *
 * start 和 end 使用半开区间 [start, end) 表示；对于字符串，区间不包含
 * 两侧的双引号；对于对象和数组，区间包含其定界符。
 */
typedef struct
{
    /** 节点的 JSON 语义类别。 */
    json_token_type_t type;
    /** 节点文本在 json_document_t::data 中的起始偏移。 */
    uint32_t start;
    /** 节点文本结束后的偏移，即半开区间的上界。 */
    uint32_t end;
    /** 父对象或父数组的 Token 索引；根节点为 -1。 */
    int32_t parent;
    /** 对象的键值对数量或数组的直接元素数量。 */
    uint16_t child_count;
    /** 非零表示该字符串 Token 是对象成员键，而不是普通字符串值。 */
    uint8_t is_key;
} json_token_t;

/**
 * 已解析 JSON 文档的零拷贝视图。
 *
 * data 和 tokens 均由调用方持有，且在本结构被使用期间必须保持有效。
 */
typedef struct
{
    /** 原始 JSON 字节缓冲区，只读且不以空字符结尾为前提。 */
    const uint8_t *data;
    /** 原始 JSON 缓冲区的有效字节数。 */
    uint32_t size;
    /** 调用方提供的 Token 存储区。 */
    json_token_t *tokens;
    /** Token 存储区可容纳的最大 Token 数。 */
    uint32_t token_capacity;
    /** 已写入 Token 的数量。 */
    uint32_t token_count;
} json_document_t;

/**
 * 接收规范化 JSON 片段的流式输出回调。
 *
 * @param context 调用方自定义的输出上下文，可为 NULL。
 * @param data    待输出字节的首地址。
 * @param size    待输出字节数。
 * @return FIRMWARE_STATUS_OK 表示片段已接收；其他状态会终止规范化过程。
 */
typedef firmware_status_t (*json_canonical_sink_fn)(void *context, const void *data, size_t size);

/**
 * 解析一份完整的严格 JSON 文档，并拒绝任意对象内的重复键。
 *
 * @param document       接收已解析零拷贝文档视图的输出对象。
 * @param data           JSON 原始字节缓冲区；调用返回后仍须保持有效。
 * @param size           data 中的有效字节数，必须大于零。
 * @param tokens         调用方提供的 Token 存储区。
 * @param token_capacity tokens 可容纳的 Token 数量，必须大于零。
 * @return FIRMWARE_STATUS_OK 表示完整根对象解析成功；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数无效；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示深度、Token 容量或数值范围不足；
 *         FIRMWARE_STATUS_NOT_SUPPORTED 或 FIRMWARE_STATUS_INVALID_STATE 表示输入不符合受支持的严格
 * JSON 子集。
 */
firmware_status_t JsonDocument_Parse(json_document_t *document, const uint8_t *data, uint32_t size,
                                     json_token_t *tokens, uint32_t token_capacity);

/**
 * 查找指定对象的成员，并返回其值 Token 的索引。
 *
 * @param document     已成功解析的文档。
 * @param object_index 待查询对象的 Token 索引。
 * @param key          以空字符结尾的 ASCII 成员键。
 * @param value_index  接收对应值 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示找到成员；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或对象索引无效；
 *         FIRMWARE_STATUS_INVALID_STATE 表示成员不存在或 Token 结构损坏。
 */
firmware_status_t JsonDocument_FindMember(const json_document_t *document, uint32_t object_index,
                                          const char *key, uint32_t *value_index);

/**
 * 返回数组中按输入顺序编号的直接元素。
 *
 * @param document      已成功解析的文档。
 * @param array_index   待查询数组的 Token 索引。
 * @param element_index 目标直接元素的零基序号。
 * @param value_index   接收元素 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示找到元素；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或数组索引无效；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示序号超出数组直接元素范围。
 */
firmware_status_t JsonDocument_ArrayGet(const json_document_t *document, uint32_t array_index,
                                        uint32_t element_index, uint32_t *value_index);

/**
 * 复制一个字符串 Token 的 ASCII 内容，并补写空字符结束符。
 *
 * @param document         已成功解析的文档。
 * @param token_index      字符串 Token 的索引。
 * @param destination      调用方提供的目标字符缓冲区。
 * @param destination_size destination 的总容量，包含结尾空字符所需空间。
 * @return FIRMWARE_STATUS_OK 表示复制成功；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或索引无效；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示 Token 不是字符串或目标缓冲区不足。
 */
firmware_status_t JsonDocument_CopyString(const json_document_t *document, uint32_t token_index,
                                          char *destination, uint32_t destination_size);

/**
 * 将严格 JSON 中的非负十进制整数 Token 转换为 uint32_t。
 *
 * @param document    已成功解析的文档。
 * @param token_index 数字 Token 的索引。
 * @param value       接收转换结果的输出参数。
 * @return FIRMWARE_STATUS_OK 表示转换成功；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或索引无效；
 *         FIRMWARE_STATUS_INVALID_STATE 表示 Token 不是数字；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示数值超过 uint32_t 的可表示范围。
 */
firmware_status_t JsonDocument_GetU32(const json_document_t *document, uint32_t token_index,
                                      uint32_t *value);

/**
 * 读取 JSON 布尔 Token。
 *
 * @param document    已成功解析的文档。
 * @param token_index 布尔 Token 的索引。
 * @param value       接收结果的输出参数；true 写入 1，false 写入 0。
 * @return FIRMWARE_STATUS_OK 表示读取成功；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或索引无效；
 *         FIRMWARE_STATUS_INVALID_STATE 表示 Token 不是布尔值。
 */
firmware_status_t JsonDocument_GetBoolean(const json_document_t *document, uint32_t token_index,
                                          int *value);

/**
 * 将字符串 Token 与一个以空字符结尾的 ASCII 字面量进行精确比较。
 *
 * @param document    已成功解析的文档。
 * @param token_index 待比较字符串 Token 的索引。
 * @param value       待匹配的 ASCII 字面量。
 * @return 非零表示完全相等；零表示参数无效、Token 不是字符串或内容不同。
 */
int JsonDocument_StringEquals(const json_document_t *document, uint32_t token_index,
                              const char *value);

/**
 * 输出本严格 ASCII/uint32 JSON 子集支持的 RFC 8785 风格规范化形式。
 *
 * 对象成员以字典序输出，数组元素保留输入顺序，所有空白被移除。可选择从
 * 一个指定对象中排除一个成员，供计算签名或哈希时排除签名字段使用。
 *
 * @param document              已成功解析的文档。
 * @param excluded_object_index 要排除成员的对象 Token 索引；传入
 *                              JSON_DOCUMENT_NO_TOKEN 表示不排除任何成员。
 * @param excluded_member       待排除的成员键；不排除成员时可为 NULL。
 * @param sink                  接收规范化输出片段的回调。
 * @param sink_context          传递给 sink 的调用方上下文。
 * @return FIRMWARE_STATUS_OK 表示全部输出成功；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示参数或排除对象无效；
 *         其他状态由 sink 回调原样传播。
 */
firmware_status_t JsonDocument_Canonicalize(const json_document_t *document,
                                            uint32_t excluded_object_index,
                                            const char *excluded_member,
                                            json_canonical_sink_fn sink, void *sink_context);

#endif
