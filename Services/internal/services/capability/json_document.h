/**
 * @file json_document.h
 * @brief Allocation-free strict JSON document and canonicalization helpers.
 */
#ifndef SERVICES_JSON_DOCUMENT_H
#define SERVICES_JSON_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#define JSON_DOCUMENT_NO_TOKEN UINT32_MAX

typedef enum
{
    JSON_TOKEN_OBJECT = 0,
    JSON_TOKEN_ARRAY,
    JSON_TOKEN_STRING,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_TRUE,
    JSON_TOKEN_FALSE,
    JSON_TOKEN_NULL
} json_token_type_t;

typedef struct
{
    json_token_type_t type;
    uint32_t start;
    uint32_t end;
    int32_t parent;
    uint16_t child_count;
    uint8_t is_key;
} json_token_t;

typedef struct
{
    const uint8_t *data;
    uint32_t size;
    json_token_t *tokens;
    uint32_t token_capacity;
    uint32_t token_count;
} json_document_t;

typedef firmware_status_t (*json_canonical_sink_fn)(
    void *context,
    const void *data,
    size_t size);

/** Parse one complete strict JSON document and reject duplicate object keys. */
firmware_status_t JsonDocument_Parse(
    json_document_t *document,
    const uint8_t *data,
    uint32_t size,
    json_token_t *tokens,
    uint32_t token_capacity);

/** Find one object member and return its value token index. */
firmware_status_t JsonDocument_FindMember(
    const json_document_t *document,
    uint32_t object_index,
    const char *key,
    uint32_t *value_index);

/** Return the indexed direct child of an array. */
firmware_status_t JsonDocument_ArrayGet(
    const json_document_t *document,
    uint32_t array_index,
    uint32_t element_index,
    uint32_t *value_index);

/** Copy an ASCII string token and append a null terminator. */
firmware_status_t JsonDocument_CopyString(
    const json_document_t *document,
    uint32_t token_index,
    char *destination,
    uint32_t destination_size);

/** Parse a canonical nonnegative decimal integer into uint32_t. */
firmware_status_t JsonDocument_GetU32(
    const json_document_t *document,
    uint32_t token_index,
    uint32_t *value);

/** Read a JSON boolean token. */
firmware_status_t JsonDocument_GetBoolean(
    const json_document_t *document,
    uint32_t token_index,
    int *value);

/** Compare a string token with one null-terminated ASCII literal. */
int JsonDocument_StringEquals(
    const json_document_t *document,
    uint32_t token_index,
    const char *value);

/**
 * Emit the RFC 8785 canonical form supported by this strict ASCII/uint32 parser.
 * One optional member can be omitted from a selected object token.
 */
firmware_status_t JsonDocument_Canonicalize(
    const json_document_t *document,
    uint32_t excluded_object_index,
    const char *excluded_member,
    json_canonical_sink_fn sink,
    void *sink_context);

#endif
