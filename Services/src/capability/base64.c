/**
 * @file base64.c
 * @brief 不使用动态内存的严格 RFC 4648 Base64 解码实现。
 *
 * 本文件用于将 Manifest 等受信任输入中的 Base64 文本还原为二进制数据。
 * 除了完成普通解码外，还拒绝非法字符、错误位置的填充符以及非零的未使用位，
 * 从而只接受规范（canonical）编码，避免同一二进制内容存在多种可接受文本形式。
 */
#include "services/capability/base64.h"

#include <stddef.h>

/**
 * @brief 将一个非填充 Base64 字符转换为其 6 位数值。
 *
 * @param value 待转换的 ASCII 字符；调用者须先排除填充字符 '='。
 *
 * @return 成功时返回 [0, 63] 内的 Base64 索引；字符不属于 RFC 4648 基础字母表时
 *         返回 -1。
 *
 * 字母、数字以及 '+'、'/' 分别对应 RFC 4648 规定的连续取值区间。本函数不接受
 * URL-safe 字母表、空白字符或换行符，保证上层按固定格式处理受信任数据。
 */
static int DecodeCharacter(char value)
{
    if ((value >= 'A') && (value <= 'Z'))
    {
        return value - 'A';
    }
    if ((value >= 'a') && (value <= 'z'))
    {
        return value - 'a' + 26;
    }
    if ((value >= '0') && (value <= '9'))
    {
        return value - '0' + 52;
    }
    if (value == '+')
    {
        return 62;
    }
    if (value == '/')
    {
        return 63;
    }
    return -1;
}

/**
 * @brief 严格解码一段完整的 RFC 4648 Base64 文本。
 *
 * @param encoded 指向待解码文本的首字符，不能为 NULL。
 * @param encoded_size 文本长度，必须非零且为 4 的整数倍。
 * @param decoded 调用者提供的输出缓冲区，不能为 NULL。
 * @param decoded_capacity 输出缓冲区的容量，单位为字节。
 * @param decoded_size 成功时写入实际输出字节数的地址，不能为 NULL。
 *
 * @return FIRMWARE_STATUS_OK 表示解码完成；FIRMWARE_STATUS_INVALID_ARGUMENT 表示
 *         指针参数无效；FIRMWARE_STATUS_INVALID_STATE 表示长度、字符、填充位置或
 *         规范填充位不符合 RFC 4648；FIRMWARE_STATUS_OUT_OF_RANGE 表示输出缓冲区
 *         容量不足。
 *
 * 处理按四字符一组进行。最后一组最多允许两个位于末尾的 '='，并检查这些填充
 * 对应的未使用位为零，避免接受可以解出相同字节但文本表示不同的非规范编码。
 * 函数仅在全部输入校验并写入成功后更新 @p decoded_size。
 */
firmware_status_t Base64_DecodeStrict(const char *encoded, size_t encoded_size, uint8_t *decoded,
                                      size_t decoded_capacity, size_t *decoded_size)
{
    size_t output_size;
    size_t padding = 0U;
    size_t input_offset;
    size_t output_offset = 0U;

    if ((encoded == NULL) || (decoded == NULL) || (decoded_size == NULL))
    {
        /* 输出长度属于调用结果，所有输入/输出指针均必须有效。 */
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((encoded_size == 0U) || ((encoded_size & 3U) != 0U))
    {
        /* RFC 4648 的完整 Base64 量子固定为四个字符。 */
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (encoded[encoded_size - 1U] == '=')
    {
        padding = 1U;
        if (encoded[encoded_size - 2U] == '=')
        {
            padding = 2U;
        }
    }
    output_size = (encoded_size / 4U) * 3U - padding;
    if (output_size > decoded_capacity)
    {
        /* 先完成容量判断，防止部分写入后才发现调用者缓冲区不足。 */
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    /* 每个四字符量子最多还原三个字节。 */
    for (input_offset = 0U; input_offset < encoded_size; input_offset += 4U)
    {
        int values[4];
        size_t index;
        int is_last = (input_offset + 4U == encoded_size);

        for (index = 0U; index < 4U; ++index)
        {
            char character = encoded[input_offset + index];

            if (character == '=')
            {
                /* 填充符只能出现在最后一个量子的后两个位置。 */
                if (!is_last || (index < 2U))
                {
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
                values[index] = 0;
            }
            else
            {
                values[index] = DecodeCharacter(character);
                if (values[index] < 0)
                {
                    /* 不接受空白、换行和 URL-safe 等非基础字母表字符。 */
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
            }
        }
        if ((encoded[input_offset + 2U] == '=') && (encoded[input_offset + 3U] != '='))
        {
            /* 两个填充符必须连续出现，不能形成 "=x" 的尾部。 */
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((encoded[input_offset + 2U] == '=') && ((values[1] & 0x0F) != 0))
        {
            /* 一个输出字节时，第二个 6 位值的低四位必须为零。 */
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((encoded[input_offset + 3U] == '=') && (encoded[input_offset + 2U] != '=') &&
            ((values[2] & 0x03) != 0))
        {
            /* 两个输出字节时，第三个 6 位值的低两位必须为零。 */
            return FIRMWARE_STATUS_INVALID_STATE;
        }

        /* 根据最终输出长度跳过由尾部填充符省略的字节。 */
        if (output_offset < output_size)
        {
            decoded[output_offset++] = (uint8_t) ((values[0] << 2) | (values[1] >> 4));
        }
        if (output_offset < output_size)
        {
            decoded[output_offset++] = (uint8_t) ((values[1] << 4) | (values[2] >> 2));
        }
        if (output_offset < output_size)
        {
            decoded[output_offset++] = (uint8_t) ((values[2] << 6) | values[3]);
        }
    }
    *decoded_size = output_size;
    return FIRMWARE_STATUS_OK;
}
