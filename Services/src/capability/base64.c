/**
 * @file base64.c
 * @brief Allocation-free canonical RFC 4648 Base64 decoding.
 */
#include "services/capability/base64.h"

#include <stddef.h>

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

firmware_status_t Base64_DecodeStrict(
    const char *encoded,
    size_t encoded_size,
    uint8_t *decoded,
    size_t decoded_capacity,
    size_t *decoded_size)
{
    size_t output_size;
    size_t padding = 0U;
    size_t input_offset;
    size_t output_offset = 0U;

    if ((encoded == NULL) || (decoded == NULL) || (decoded_size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((encoded_size == 0U) || ((encoded_size & 3U) != 0U))
    {
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
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

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
                    return FIRMWARE_STATUS_INVALID_STATE;
                }
            }
        }
        if ((encoded[input_offset + 2U] == '=') &&
            (encoded[input_offset + 3U] != '='))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((encoded[input_offset + 2U] == '=') && ((values[1] & 0x0F) != 0))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((encoded[input_offset + 3U] == '=') &&
            (encoded[input_offset + 2U] != '=') && ((values[2] & 0x03) != 0))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }

        if (output_offset < output_size)
        {
            decoded[output_offset++] =
                (uint8_t)((values[0] << 2) | (values[1] >> 4));
        }
        if (output_offset < output_size)
        {
            decoded[output_offset++] =
                (uint8_t)((values[1] << 4) | (values[2] >> 2));
        }
        if (output_offset < output_size)
        {
            decoded[output_offset++] =
                (uint8_t)((values[2] << 6) | values[3]);
        }
    }
    *decoded_size = output_size;
    return FIRMWARE_STATUS_OK;
}
