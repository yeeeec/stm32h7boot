/**
 * @file json_document.c
 * @brief Strict JSON parser and canonical emitter for signed manifests.
 */
#include "services/capability/json_document.h"

#include <limits.h>
#include <string.h>

#define JSON_MAX_DEPTH 16U

typedef struct
{
    json_document_t *document;
    uint32_t position;
    uint32_t depth;
} json_parser_t;

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

static firmware_status_t AllocateToken(json_parser_t *parser, json_token_type_t type,
                                       int32_t parent, uint32_t start, uint32_t *token_index)
{
    json_token_t *token;

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

static firmware_status_t ParseValue(json_parser_t *parser, int32_t parent, uint32_t *token_index);

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
        /* Manifest strings are deliberately restricted to unescaped ASCII. */
        if ((value < 0x20U) || (value > 0x7EU) || (value == '\\'))
        {
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        }
        ++parser->position;
    }
    return FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t ParseNumber(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t start = parser->position;
    firmware_status_t status;

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

static int MatchLiteral(const json_parser_t *parser, const char *literal, uint32_t length)
{
    return (parser->position <= parser->document->size) &&
           (length <= (parser->document->size - parser->position)) &&
           (memcmp(&parser->document->data[parser->position], literal, length) == 0);
}

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

static firmware_status_t ParseObject(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t object_index;
    firmware_status_t status;

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
        if (parser->document->data[parser->position++] != ',')
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        SkipWhitespace(parser);
    }
}

static firmware_status_t ParseArray(json_parser_t *parser, int32_t parent, uint32_t *token_index)
{
    uint32_t array_index;
    firmware_status_t status;

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
        if (parser->document->data[parser->position++] != ',')
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        SkipWhitespace(parser);
    }
}

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

static int TokenStringEquals(const json_document_t *document, const json_token_t *token,
                             const char *value)
{
    size_t length = strlen(value);

    return (token->type == JSON_TOKEN_STRING) && (length == (size_t) (token->end - token->start)) &&
           (memcmp(&document->data[token->start], value, length) == 0);
}

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

firmware_status_t JsonDocument_Parse(json_document_t *document, const uint8_t *data, uint32_t size,
                                     json_token_t *tokens, uint32_t token_capacity)
{
    json_parser_t parser;
    uint32_t root_index;
    firmware_status_t status;

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
    if ((root_index != 0U) || (parser.position != size) || (tokens[0].type != JSON_TOKEN_OBJECT))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return RejectDuplicateKeys(document);
}

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
    if ((token->type != JSON_TOKEN_STRING) || (destination_size == 0U) ||
        (length >= destination_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(destination, &document->data[token->start], length);
    destination[length] = '\0';
    return FIRMWARE_STATUS_OK;
}

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

        if (result > ((UINT32_MAX - digit) / 10U))
        {
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return FIRMWARE_STATUS_OK;
}

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

int JsonDocument_StringEquals(const json_document_t *document, uint32_t token_index,
                              const char *value)
{
    return (document != NULL) && (value != NULL) && (token_index < document->token_count) &&
           TokenStringEquals(document, &document->tokens[token_index], value);
}

static firmware_status_t Emit(json_canonical_sink_fn sink, void *context, const void *data,
                              size_t size)
{
    return sink(context, data, size);
}

static firmware_status_t CanonicalizeToken(const json_document_t *document, uint32_t token_index,
                                           uint32_t excluded_object_index,
                                           const char *excluded_member, json_canonical_sink_fn sink,
                                           void *sink_context);

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

static firmware_status_t CanonicalizeToken(const json_document_t *document, uint32_t token_index,
                                           uint32_t excluded_object_index,
                                           const char *excluded_member, json_canonical_sink_fn sink,
                                           void *sink_context)
{
    const json_token_t *token;
    firmware_status_t status;

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

firmware_status_t JsonDocument_Canonicalize(const json_document_t *document,
                                            uint32_t excluded_object_index,
                                            const char *excluded_member,
                                            json_canonical_sink_fn sink, void *sink_context)
{
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
