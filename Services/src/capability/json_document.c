/**
 * @file json_document.c
 * @brief 用于签名清单的严格 JSON 解析器与规范化输出器。
 *
 * 实现不分配动态内存，所有 Token 由调用方提供。为保证清单签名输入唯一，
 * 解析阶段限制字符串、数字和嵌套深度，并拒绝对象中的重复成员键。
 */
#include "services/capability/json_document.h"

#include <limits.h>
#include <string.h>

/** 允许的对象/数组最大嵌套层级，防止畸形输入耗尽调用栈或 Token 缓冲区。 */
#define JSON_MAX_DEPTH 16U

/** 严格 JSON 解析过程中的可变游标状态。 */
typedef struct
{
    /** 当前正在写入的零拷贝文档及其 Token 存储区。 */
    json_document_t *document;
    /** 原始 JSON 缓冲区中下一个待读取字节的偏移。 */
    uint32_t position;
    /** 当前对象/数组嵌套层数。 */
    uint32_t depth;
} json_parser_t;

/**
 * 跳过当前位置开始的 JSON 空白字符。
 *
 * @param parser 解析器状态；调用后 position 指向下一个非空白字符或输入末尾。
 */
static void SkipWhitespace(json_parser_t *parser)
{
    while (parser->position < parser->document->size)
    {
        uint8_t value = parser->document->data[parser->position];

        if ((value != ' ') && (value != '\t') && (value != '\r') && (value != '\n'))
        {
            break;
        }
        ++parser->position;
    }
}

/**
 * 在调用方提供的 Token 数组中分配并初始化一个节点。
 *
 * @param parser      解析器状态及目标 Token 存储区。
 * @param type        新节点的 JSON 类型。
 * @param parent      父对象或父数组的 Token 索引；根节点传入 -1。
 * @param start       节点文本的起始偏移。
 * @param token_index 接收新 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示分配成功；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示 Token 存储区已满。
 */
static firmware_status_t AllocateToken(json_parser_t *parser, json_token_type_t type,
                                       int32_t parent, uint32_t start, uint32_t *token_index)
{
    json_token_t *token;

    /* Token 数量受调用方静态缓冲区限制，不能越界写入。 */
    if (parser->document->token_count >= parser->document->token_capacity)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    *token_index       = parser->document->token_count++;
    token              = &parser->document->tokens[*token_index];
    token->type        = type;
    token->start       = start;
    token->end         = start;
    token->parent      = parent;
    token->child_count = 0U;
    token->is_key      = 0U;
    return FIRMWARE_STATUS_OK;
}

/**
 * 按当前位置的首字符分派并解析一个 JSON 值。
 *
 * @param parser      解析器状态。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收值 Token 索引的输出参数。
 * @return 各具体解析函数返回的状态；不支持的值类型返回 FIRMWARE_STATUS_NOT_SUPPORTED。
 */
static firmware_status_t ParseValue(json_parser_t *parser, int32_t parent, uint32_t *token_index);

/**
 * 解析一个未转义的 ASCII JSON 字符串。
 *
 * @param parser      解析器状态，入口时 position 指向起始双引号。
 * @param parent      父对象或父数组的 Token 索引。
 * @param is_key      非零表示该字符串是对象成员键。
 * @param token_index 接收字符串 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示解析完成；
 *         FIRMWARE_STATUS_NOT_SUPPORTED 表示包含控制字符、非 ASCII 或转义符；
 *         FIRMWARE_STATUS_INVALID_STATE 表示字符串未闭合；
 *         也可能返回 Token 分配错误。
 */
static firmware_status_t ParseString(json_parser_t *parser, int32_t parent, int is_key,
                                     uint32_t *token_index)
{
    firmware_status_t status;

    ++parser->position;
    status = AllocateToken(parser, JSON_TOKEN_STRING, parent, parser->position, token_index);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    parser->document->tokens[*token_index].is_key = (uint8_t) is_key;
    while (parser->position < parser->document->size)
    {
        uint8_t value = parser->document->data[parser->position];

        if (value == '"')
        {
            parser->document->tokens[*token_index].end = parser->position;
            ++parser->position;
            return FIRMWARE_STATUS_OK;
        }
        /* 清单字符串刻意限定为未转义 ASCII，确保签名文本的表示唯一。 */
        if ((value < 0x20U) || (value > 0x7EU) || (value == '\\'))
        {
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        }
        ++parser->position;
    }
    return FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 解析一个严格的非负十进制 JSON 数字。
 *
 * @param parser      解析器状态，入口时 position 指向第一个数字。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收数字 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示解析完成；
 *         FIRMWARE_STATUS_INVALID_STATE 表示出现前导零；
 *         也可能返回 Token 分配错误。
 */
static firmware_status_t ParseNumber(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t start = parser->position;
    firmware_status_t status;

    /* 仅允许数字 0 自身，禁止 01 等非规范化前导零表示。 */
    if (parser->document->data[parser->position] == '0')
    {
        ++parser->position;
        if ((parser->position < parser->document->size) &&
            (parser->document->data[parser->position] >= '0') &&
            (parser->document->data[parser->position] <= '9'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    else
    {
        while ((parser->position < parser->document->size) &&
               (parser->document->data[parser->position] >= '0') &&
               (parser->document->data[parser->position] <= '9'))
        {
            ++parser->position;
        }
    }
    status = AllocateToken(parser, JSON_TOKEN_NUMBER, parent, start, token_index);
    if (FirmwareStatus_IsOk(status))
    {
        parser->document->tokens[*token_index].end = parser->position;
    }
    return status;
}

/**
 * 判断当前位置后的字节是否与指定 JSON 字面量完全匹配。
 *
 * @param parser  只读解析器状态。
 * @param literal 待匹配字面量的首地址。
 * @param length  待匹配字面量长度。
 * @return 非零表示完全匹配；零表示剩余长度不足或文本不同。
 */
static int MatchLiteral(const json_parser_t *parser, const char *literal, uint32_t length)
{
    return (parser->position <= parser->document->size) &&
           (length <= (parser->document->size - parser->position)) &&
           (memcmp(&parser->document->data[parser->position], literal, length) == 0);
}

/**
 * 解析 true、false 或 null 字面量。
 *
 * @param parser      解析器状态，入口时 position 指向字面量首字符。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收字面量 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示解析完成；
 *         FIRMWARE_STATUS_INVALID_STATE 表示不是支持的 JSON 字面量；
 *         也可能返回 Token 分配错误。
 */
static firmware_status_t ParseLiteral(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    json_token_type_t type;
    uint32_t length;
    firmware_status_t status;

    if (MatchLiteral(parser, "true", 4U))
    {
        type   = JSON_TOKEN_TRUE;
        length = 4U;
    }
    else if (MatchLiteral(parser, "false", 5U))
    {
        type   = JSON_TOKEN_FALSE;
        length = 5U;
    }
    else if (MatchLiteral(parser, "null", 4U))
    {
        type   = JSON_TOKEN_NULL;
        length = 4U;
    }
    else
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = AllocateToken(parser, type, parent, parser->position, token_index);
    if (FirmwareStatus_IsOk(status))
    {
        parser->position += length;
        parser->document->tokens[*token_index].end = parser->position;
    }
    return status;
}

/**
 * 解析一个 JSON 对象及其直接键值对。
 *
 * @param parser      解析器状态，入口时 position 指向左花括号。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收对象 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示对象闭合且解析完成；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示嵌套过深或 Token 不足；
 *         FIRMWARE_STATUS_INVALID_STATE 表示对象语法不完整或分隔符错误；
 *         其他状态由子值解析原样传播。
 */
static firmware_status_t ParseObject(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t object_index;
    firmware_status_t status;

    /* 深度在分配 Token 前检查，避免恶意嵌套继续消耗资源。 */
    if (++parser->depth > JSON_MAX_DEPTH)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    status = AllocateToken(parser, JSON_TOKEN_OBJECT, parent, parser->position, &object_index);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    ++parser->position;
    SkipWhitespace(parser);
    if ((parser->position < parser->document->size) &&
        (parser->document->data[parser->position] == '}'))
    {
        parser->document->tokens[object_index].end = ++parser->position;
        --parser->depth;
        *token_index = object_index;
        return FIRMWARE_STATUS_OK;
    }

    for (;;)
    {
        uint32_t key_index;
        uint32_t value_index;

        /* JSON 对象只允许字符串键，随后必须紧跟冒号和值。 */
        if ((parser->position >= parser->document->size) ||
            (parser->document->data[parser->position] != '"'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        status = ParseString(parser, (int32_t) object_index, 1, &key_index);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        SkipWhitespace(parser);
        if ((parser->position >= parser->document->size) ||
            (parser->document->data[parser->position++] != ':'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        SkipWhitespace(parser);
        status = ParseValue(parser, (int32_t) object_index, &value_index);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        (void) key_index;
        (void) value_index;
        ++parser->document->tokens[object_index].child_count;
        SkipWhitespace(parser);
        if (parser->position >= parser->document->size)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if (parser->document->data[parser->position] == '}')
        {
            parser->document->tokens[object_index].end = ++parser->position;
            --parser->depth;
            *token_index = object_index;
            return FIRMWARE_STATUS_OK;
        }
        /* 每个非末尾键值对之后必须由逗号分隔。 */
        if (parser->document->data[parser->position++] != ',')
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        SkipWhitespace(parser);
    }
}

/**
 * 解析一个 JSON 数组及其直接元素。
 *
 * @param parser      解析器状态，入口时 position 指向左方括号。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收数组 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示数组闭合且解析完成；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示嵌套过深或 Token 不足；
 *         FIRMWARE_STATUS_INVALID_STATE 表示数组语法不完整或分隔符错误；
 *         其他状态由元素解析原样传播。
 */
static firmware_status_t ParseArray(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t array_index;
    firmware_status_t status;

    /* 与对象共用同一深度预算，限制任意复合 JSON 结构。 */
    if (++parser->depth > JSON_MAX_DEPTH)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    status = AllocateToken(parser, JSON_TOKEN_ARRAY, parent, parser->position, &array_index);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    ++parser->position;
    SkipWhitespace(parser);
    if ((parser->position < parser->document->size) &&
        (parser->document->data[parser->position] == ']'))
    {
        parser->document->tokens[array_index].end = ++parser->position;
        --parser->depth;
        *token_index = array_index;
        return FIRMWARE_STATUS_OK;
    }

    for (;;)
    {
        uint32_t value_index;

        status = ParseValue(parser, (int32_t) array_index, &value_index);
        if (!FirmwareStatus_IsOk(status))
        {
            return status;
        }
        (void) value_index;
        ++parser->document->tokens[array_index].child_count;
        SkipWhitespace(parser);
        if (parser->position >= parser->document->size)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if (parser->document->data[parser->position] == ']')
        {
            parser->document->tokens[array_index].end = ++parser->position;
            --parser->depth;
            *token_index = array_index;
            return FIRMWARE_STATUS_OK;
        }
        /* 非末尾数组元素后只能接受逗号。 */
        if (parser->document->data[parser->position++] != ',')
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        SkipWhitespace(parser);
    }
}

/**
 * 按 JSON 值首字符选择对象、数组、字符串、字面量或数字解析器。
 *
 * @param parser      解析器状态。
 * @param parent      父对象或父数组的 Token 索引。
 * @param token_index 接收值 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示子值解析成功；
 *         FIRMWARE_STATUS_INVALID_STATE 表示输入已结束；
 *         FIRMWARE_STATUS_NOT_SUPPORTED 表示值类型不在受支持子集内；
 *         其他状态由具体解析器返回。
 */
static firmware_status_t ParseValue(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    SkipWhitespace(parser);
    if (parser->position >= parser->document->size)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    switch (parser->document->data[parser->position])
    {
        case '{':
            return ParseObject(parser, parent, token_index);
        case '[':
            return ParseArray(parser, parent, token_index);
        case '"':
            return ParseString(parser, parent, 0, token_index);
        case 't':
        case 'f':
        case 'n':
            return ParseLiteral(parser, parent, token_index);
        default:
            if ((parser->document->data[parser->position] >= '0') &&
                (parser->document->data[parser->position] <= '9'))
            {
                return ParseNumber(parser, parent, token_index);
            }
            return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
}

/**
 * 将字符串 Token 与以空字符结尾的文本进行精确比较。
 *
 * @param document 已解析文档。
 * @param token    待比较 Token，必须为字符串。
 * @param value    待比较的空字符结尾文本。
 * @return 非零表示长度和每个字节都相同；零表示类型、长度或内容不同。
 */
static int TokenStringEquals(const json_document_t *document, const json_token_t *token,
                             const char *value)
{
    size_t length = strlen(value);

    return (token->type == JSON_TOKEN_STRING) && (length == (size_t) (token->end - token->start)) &&
           (memcmp(&document->data[token->start], value, length) == 0);
}

/**
 * 以无符号字节字典序比较两个字符串 Token。
 *
 * @param document  已解析文档。
 * @param lhs_index 左侧字符串 Token 索引。
 * @param rhs_index 右侧字符串 Token 索引。
 * @return 小于零表示 lhs 小于 rhs；零表示相等；大于零表示 lhs 大于 rhs。
 */
static int CompareTokenStrings(const json_document_t *document, uint32_t lhs_index,
                               uint32_t rhs_index)
{
    const json_token_t *lhs = &document->tokens[lhs_index];
    const json_token_t *rhs = &document->tokens[rhs_index];
    uint32_t lhs_length     = lhs->end - lhs->start;
    uint32_t rhs_length     = rhs->end - rhs->start;
    uint32_t common_length  = (lhs_length < rhs_length) ? lhs_length : rhs_length;
    int comparison =
        memcmp(&document->data[lhs->start], &document->data[rhs->start], common_length);

    if (comparison != 0)
    {
        return comparison;
    }
    return (lhs_length < rhs_length) ? -1 : (lhs_length > rhs_length) ? 1 : 0;
}

/**
 * 遍历全部对象，拒绝同一对象内文本相同的成员键。
 *
 * @param document 已完成基础语法解析的文档。
 * @return FIRMWARE_STATUS_OK 表示未发现重复键；
 *         FIRMWARE_STATUS_INVALID_STATE 表示存在重复对象成员键。
 */
static firmware_status_t RejectDuplicateKeys(const json_document_t *document)
{
    uint32_t object_index;

    for (object_index = 0U; object_index < document->token_count; ++object_index)
    {
        uint32_t lhs;

        if (document->tokens[object_index].type != JSON_TOKEN_OBJECT)
        {
            continue;
        }
        for (lhs = 0U; lhs < document->token_count; ++lhs)
        {
            uint32_t rhs;

            if ((document->tokens[lhs].parent != (int32_t) object_index) ||
                (document->tokens[lhs].is_key == 0U))
            {
                continue;
            }
            for (rhs = lhs + 1U; rhs < document->token_count; ++rhs)
            {
                /* 对同一父对象的每对成员键作精确比较，保证签名输入无歧义。 */
                if ((document->tokens[rhs].parent == (int32_t) object_index) &&
                    (document->tokens[rhs].is_key != 0U) &&
                    (CompareTokenStrings(document, lhs, rhs) == 0))
                {
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
            }
        }
    }
    return FIRMWARE_STATUS_OK;
}

/**
 * 解析完整的严格 JSON 根对象并建立零拷贝 Token 文档。
 *
 * @param document       接收文档视图的输出对象。
 * @param data           原始 JSON 数据。
 * @param size           原始数据长度。
 * @param tokens         调用方提供的 Token 存储区。
 * @param token_capacity Token 存储区容量。
 * @return FIRMWARE_STATUS_OK 表示解析成功；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_Parse(json_document_t *document, const uint8_t *data, uint32_t size,
                                     json_token_t *tokens, uint32_t token_capacity)
{
    json_parser_t parser;
    uint32_t root_index;
    firmware_status_t status;

    /* 缓冲区和 Token 存储均由调用方提供，任一缺失均不能开始解析。 */
    if ((document == NULL) || (data == NULL) || (size == 0U) || (tokens == NULL) ||
        (token_capacity == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    document->data           = data;
    document->size           = size;
    document->tokens         = tokens;
    document->token_capacity = token_capacity;
    document->token_count    = 0U;
    parser.document          = document;
    parser.position          = 0U;
    parser.depth             = 0U;
    status                   = ParseValue(&parser, -1, &root_index);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    SkipWhitespace(&parser);
    /* 清单根必须是唯一完整对象，拒绝尾随内容和非对象根节点。 */
    if ((root_index != 0U) || (parser.position != size) || (tokens[0].type != JSON_TOKEN_OBJECT))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return RejectDuplicateKeys(document);
}

/**
 * 在一个对象的直接成员中查找给定键，并返回紧随该键的值 Token。
 *
 * @param document     已解析文档。
 * @param object_index 对象 Token 索引。
 * @param key          待查找的空字符结尾键名。
 * @param value_index  接收值 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示找到成员；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_FindMember(const json_document_t *document, uint32_t object_index,
                                          const char *key, uint32_t *value_index)
{
    uint32_t index;

    if ((document == NULL) || (key == NULL) || (value_index == NULL) ||
        (object_index >= document->token_count) ||
        (document->tokens[object_index].type != JSON_TOKEN_OBJECT))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    for (index = object_index + 1U; index < document->token_count; ++index)
    {
        const json_token_t *token = &document->tokens[index];

        if ((token->parent == (int32_t) object_index) && (token->is_key != 0U) &&
            TokenStringEquals(document, token, key))
        {
            /* 解析器保证键后紧跟值；此检查用于防御损坏的 Token 表。 */
            if ((index + 1U) >= document->token_count)
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
            *value_index = index + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }
    return FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 返回数组中指定序号的直接元素 Token。
 *
 * @param document      已解析文档。
 * @param array_index   数组 Token 索引。
 * @param element_index 目标元素的零基序号。
 * @param value_index   接收元素 Token 索引的输出参数。
 * @return FIRMWARE_STATUS_OK 表示找到元素；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_ArrayGet(const json_document_t *document, uint32_t array_index,
                                        uint32_t element_index, uint32_t *value_index)
{
    uint32_t index;
    uint32_t current = 0U;

    if ((document == NULL) || (value_index == NULL) || (array_index >= document->token_count) ||
        (document->tokens[array_index].type != JSON_TOKEN_ARRAY))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    for (index = array_index + 1U; index < document->token_count; ++index)
    {
        if (document->tokens[index].parent == (int32_t) array_index)
        {
            if (current++ == element_index)
            {
                *value_index = index;
                return FIRMWARE_STATUS_OK;
            }
        }
    }
    return FIRMWARE_STATUS_OUT_OF_RANGE;
}

/**
 * 将字符串 Token 复制到调用方缓冲区并追加结束符。
 *
 * @param document         已解析文档。
 * @param token_index      字符串 Token 索引。
 * @param destination      目标字符缓冲区。
 * @param destination_size 目标缓冲区总容量。
 * @return FIRMWARE_STATUS_OK 表示复制成功；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_CopyString(const json_document_t *document, uint32_t token_index,
                                          char *destination, uint32_t destination_size)
{
    const json_token_t *token;
    uint32_t length;

    if ((document == NULL) || (destination == NULL) || (token_index >= document->token_count))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    token  = &document->tokens[token_index];
    length = token->end - token->start;
    /* 预留一个字节写入空字符，避免返回未终止的 C 字符串。 */
    if ((token->type != JSON_TOKEN_STRING) || (destination_size == 0U) ||
        (length >= destination_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(destination, &document->data[token->start], length);
    destination[length] = '\0';
    return FIRMWARE_STATUS_OK;
}

/**
 * 将数字 Token 解析为 uint32_t，并检测十进制累加溢出。
 *
 * @param document    已解析文档。
 * @param token_index 数字 Token 索引。
 * @param value       接收转换结果的输出参数。
 * @return FIRMWARE_STATUS_OK 表示转换成功；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_GetU32(const json_document_t *document, uint32_t token_index,
                                      uint32_t *value)
{
    const json_token_t *token;
    uint32_t result = 0U;
    uint32_t index;

    if ((document == NULL) || (value == NULL) || (token_index >= document->token_count))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    token = &document->tokens[token_index];
    if (token->type != JSON_TOKEN_NUMBER)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = token->start; index < token->end; ++index)
    {
        uint32_t digit = (uint32_t) (document->data[index] - '0');

        /* 先判断下一次乘十加位是否越过 uint32_t 上界。 */
        if (result > ((UINT32_MAX - digit) / 10U))
        {
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return FIRMWARE_STATUS_OK;
}

/**
 * 读取 true 或 false Token，并转换为 C 风格布尔整数。
 *
 * @param document    已解析文档。
 * @param token_index 布尔 Token 索引。
 * @param value       接收 1（true）或 0（false）的输出参数。
 * @return FIRMWARE_STATUS_OK 表示读取成功；其余状态见公开头文件说明。
 */
firmware_status_t JsonDocument_GetBoolean(const json_document_t *document, uint32_t token_index,
                                          int *value)
{
    if ((document == NULL) || (value == NULL) || (token_index >= document->token_count))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (document->tokens[token_index].type == JSON_TOKEN_TRUE)
    {
        *value = 1;
        return FIRMWARE_STATUS_OK;
    }
    if (document->tokens[token_index].type == JSON_TOKEN_FALSE)
    {
        *value = 0;
        return FIRMWARE_STATUS_OK;
    }
    return FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 对外提供字符串 Token 与常量文本的安全精确比较。
 *
 * @param document    已解析文档。
 * @param token_index 字符串 Token 索引。
 * @param value       待匹配的空字符结尾文本。
 * @return 非零表示完全匹配；零表示参数无效、类型不符或内容不同。
 */
int JsonDocument_StringEquals(const json_document_t *document, uint32_t token_index,
                              const char *value)
{
    return (document != NULL) && (value != NULL) && (token_index < document->token_count) &&
           TokenStringEquals(document, &document->tokens[token_index], value);
}

/**
 * 将一个规范化 JSON 片段交给调用方输出回调。
 *
 * @param sink    输出回调。
 * @param context 输出上下文。
 * @param data    输出数据首地址。
 * @param size    输出数据字节数。
 * @return sink 返回的状态，保持原样传播。
 */
static firmware_status_t Emit(json_canonical_sink_fn sink, void *context, const void *data,
                              size_t size)
{
    return sink(context, data, size);
}

/**
 * 递归输出一个 Token 的规范化 JSON 文本。
 *
 * @param document              已解析文档。
 * @param token_index           待输出 Token 索引。
 * @param excluded_object_index 要排除成员的对象 Token 索引。
 * @param excluded_member       待排除成员键。
 * @param sink                  输出回调。
 * @param sink_context          输出回调上下文。
 * @return FIRMWARE_STATUS_OK 表示输出完成；其他状态表示参数无效或输出失败。
 */
static firmware_status_t CanonicalizeToken(const json_document_t *document, uint32_t token_index,
                                           uint32_t excluded_object_index,
                                           const char *excluded_member, json_canonical_sink_fn sink,
                                           void *sink_context);

/**
 * 以键的字典序输出对象的规范化表示。
 *
 * @param document              已解析文档。
 * @param object_index          对象 Token 索引。
 * @param excluded_object_index 要排除成员的对象 Token 索引。
 * @param excluded_member       待排除成员键。
 * @param sink                  输出回调。
 * @param sink_context          输出回调上下文。
 * @return FIRMWARE_STATUS_OK 表示对象输出完成；其他状态表示回调或子节点输出失败。
 */
static firmware_status_t CanonicalizeObject(const json_document_t *document, uint32_t object_index,
                                            uint32_t excluded_object_index,
                                            const char *excluded_member,
                                            json_canonical_sink_fn sink, void *sink_context)
{
    uint32_t emitted         = 0U;
    uint32_t previous_key    = JSON_DOCUMENT_NO_TOKEN;
    firmware_status_t status = Emit(sink, sink_context, "{", 1U);

    while (FirmwareStatus_IsOk(status))
    {
        uint32_t selected_key = JSON_DOCUMENT_NO_TOKEN;
        uint32_t index;

        for (index = object_index + 1U; index < document->token_count; ++index)
        {
            const json_token_t *token = &document->tokens[index];

            /* 每轮仅选择严格大于前一键的最小键，形成稳定字典序输出。 */
            if ((token->parent != (int32_t) object_index) || (token->is_key == 0U) ||
                ((object_index == excluded_object_index) &&
                 TokenStringEquals(document, token, excluded_member)) ||
                ((previous_key != JSON_DOCUMENT_NO_TOKEN) &&
                 (CompareTokenStrings(document, index, previous_key) <= 0)))
            {
                continue;
            }
            if ((selected_key == JSON_DOCUMENT_NO_TOKEN) ||
                (CompareTokenStrings(document, index, selected_key) < 0))
            {
                selected_key = index;
            }
        }
        if (selected_key == JSON_DOCUMENT_NO_TOKEN)
        {
            break;
        }
        if (emitted++ != 0U)
        {
            status = Emit(sink, sink_context, ",", 1U);
        }
        if (FirmwareStatus_IsOk(status))
        {
            status = Emit(sink, sink_context, "\"", 1U);
        }
        if (FirmwareStatus_IsOk(status))
        {
            const json_token_t *key = &document->tokens[selected_key];

            status = Emit(sink, sink_context, &document->data[key->start], key->end - key->start);
        }
        if (FirmwareStatus_IsOk(status))
        {
            status = Emit(sink, sink_context, "\":", 2U);
        }
        if (FirmwareStatus_IsOk(status))
        {
            status = CanonicalizeToken(document, selected_key + 1U, excluded_object_index,
                                       excluded_member, sink, sink_context);
        }
        previous_key = selected_key;
    }
    return FirmwareStatus_IsOk(status) ? Emit(sink, sink_context, "}", 1U) : status;
}

/**
 * 按原始输入顺序输出数组的规范化表示。
 *
 * @param document              已解析文档。
 * @param array_index           数组 Token 索引。
 * @param excluded_object_index 要排除成员的对象 Token 索引。
 * @param excluded_member       待排除成员键。
 * @param sink                  输出回调。
 * @param sink_context          输出回调上下文。
 * @return FIRMWARE_STATUS_OK 表示数组输出完成；其他状态表示回调或子节点输出失败。
 */
static firmware_status_t CanonicalizeArray(const json_document_t *document, uint32_t array_index,
                                           uint32_t excluded_object_index,
                                           const char *excluded_member, json_canonical_sink_fn sink,
                                           void *sink_context)
{
    uint32_t index;
    uint32_t emitted         = 0U;
    firmware_status_t status = Emit(sink, sink_context, "[", 1U);

    for (index = array_index + 1U; FirmwareStatus_IsOk(status) && (index < document->token_count);
         ++index)
    {
        if (document->tokens[index].parent != (int32_t) array_index)
        {
            continue;
        }
        if (emitted++ != 0U)
        {
            status = Emit(sink, sink_context, ",", 1U);
        }
        if (FirmwareStatus_IsOk(status))
        {
            status = CanonicalizeToken(document, index, excluded_object_index, excluded_member,
                                       sink, sink_context);
        }
    }
    return FirmwareStatus_IsOk(status) ? Emit(sink, sink_context, "]", 1U) : status;
}

/**
 * 根据 Token 类型输出对象、数组、字符串或原子值的规范化文本。
 *
 * @param document              已解析文档。
 * @param token_index           待输出 Token 索引。
 * @param excluded_object_index 要排除成员的对象 Token 索引。
 * @param excluded_member       待排除成员键。
 * @param sink                  输出回调。
 * @param sink_context          输出回调上下文。
 * @return FIRMWARE_STATUS_OK 表示输出完成；其他状态表示参数无效或输出失败。
 */
static firmware_status_t CanonicalizeToken(const json_document_t *document, uint32_t token_index,
                                           uint32_t excluded_object_index,
                                           const char *excluded_member, json_canonical_sink_fn sink,
                                           void *sink_context)
{
    const json_token_t *token;
    firmware_status_t status;

    /* 递归入口统一验证，防止损坏 Token 索引或空回调导致非法访问。 */
    if ((document == NULL) || (token_index >= document->token_count) || (sink == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    token = &document->tokens[token_index];

    if (token->type == JSON_TOKEN_OBJECT)
    {
        return CanonicalizeObject(document, token_index, excluded_object_index, excluded_member,
                                  sink, sink_context);
    }
    if (token->type == JSON_TOKEN_ARRAY)
    {
        return CanonicalizeArray(document, token_index, excluded_object_index, excluded_member,
                                 sink, sink_context);
    }
    if (token->type == JSON_TOKEN_STRING)
    {
        status = Emit(sink, sink_context, "\"", 1U);
        if (FirmwareStatus_IsOk(status))
        {
            status =
                Emit(sink, sink_context, &document->data[token->start], token->end - token->start);
        }
        return FirmwareStatus_IsOk(status) ? Emit(sink, sink_context, "\"", 1U) : status;
    }
    return Emit(sink, sink_context, &document->data[token->start], token->end - token->start);
}

/**
 * 输出整份文档的规范化 JSON，并可排除指定对象的一个成员。
 *
 * @param document              已解析文档。
 * @param excluded_object_index 要排除成员的对象 Token 索引，或 JSON_DOCUMENT_NO_TOKEN。
 * @param excluded_member       要排除的成员键；未启用排除时可为 NULL。
 * @param sink                  接收输出分片的回调。
 * @param sink_context          传递给回调的调用方上下文。
 * @return FIRMWARE_STATUS_OK 表示输出成功；其余状态见公开头文件说明或由 sink 返回。
 */
firmware_status_t JsonDocument_Canonicalize(const json_document_t *document,
                                            uint32_t excluded_object_index,
                                            const char *excluded_member,
                                            json_canonical_sink_fn sink, void *sink_context)
{
    /* 只有合法对象才允许指定排除成员，避免规范化阶段解释无效索引。 */
    if ((document == NULL) || (sink == NULL) ||
        ((excluded_object_index != JSON_DOCUMENT_NO_TOKEN) &&
         ((excluded_member == NULL) || (excluded_object_index >= document->token_count) ||
          (document->tokens[excluded_object_index].type != JSON_TOKEN_OBJECT))))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return CanonicalizeToken(document, 0U, excluded_object_index, excluded_member, sink,
                             sink_context);
}
