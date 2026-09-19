#include "update/update_request.h"

#include <string.h>

#include "firmware/boot_config.h"

typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t pos;
} cursor_t;

static void skip_ws(cursor_t *c)
{
    while (c->pos < c->size && (c->data[c->pos] == ' ' || c->data[c->pos] == '\t' ||
                                c->data[c->pos] == '\r' || c->data[c->pos] == '\n'))
        ++c->pos;
}

static int take(cursor_t *c, uint8_t ch)
{
    skip_ws(c);
    if (c->pos >= c->size || c->data[c->pos] != ch)
        return 0;
    ++c->pos;
    return 1;
}

static int string_value(cursor_t *c, char *out, size_t capacity)
{
    size_t n = 0U;
    skip_ws(c);
    if (c->pos >= c->size || c->data[c->pos++] != '"' || capacity == 0U)
        return 0;
    while (c->pos < c->size)
    {
        uint8_t ch = c->data[c->pos++];
        if (ch == '"')
        {
            out[n] = '\0';
            return 1;
        }
        if (ch < 0x20U || ch == '\\' || n + 1U >= capacity)
            return 0;
        out[n++] = (char) ch;
    }
    return 0;
}

static int uint_value(cursor_t *c, uint32_t *value)
{
    uint64_t n    = 0U;
    size_t digits = 0U;
    skip_ws(c);
    while (c->pos < c->size && c->data[c->pos] >= '0' && c->data[c->pos] <= '9')
    {
        n = n * 10U + (uint32_t) (c->data[c->pos++] - '0');
        if (++digits > 10U || n > 0xffffffffULL)
            return 0;
    }
    if (digits == 0U)
        return 0;
    *value = (uint32_t) n;
    return 1;
}

static int bool_true(cursor_t *c)
{
    static const char true_word[] = "true";
    size_t i;
    skip_ws(c);
    for (i = 0U; i < sizeof(true_word) - 1U; ++i)
        if (c->pos + i >= c->size || c->data[c->pos + i] != (uint8_t) true_word[i])
            return 0;
    c->pos += sizeof(true_word) - 1U;
    return 1;
}

int UpdateRequest_IsManifestHashValid(const char *hash)
{
    size_t i;
    if (hash == NULL || strlen(hash) != STORAGE_SHA256_HEX_LENGTH)
        return 0;
    for (i = 0U; i < STORAGE_SHA256_HEX_LENGTH; ++i)
    {
        const unsigned char ch = (unsigned char) hash[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return 0;
    }
    return 1;
}

static int package_id_valid(const char *id)
{
    size_t i;
    if (id == NULL || id[0] == '\0')
        return 0;
    for (i = 0U; i < STORAGE_PACKAGE_ID_MAX && id[i] != '\0'; ++i)
    {
        const unsigned char ch = (unsigned char) id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_' || ch == '.' || ch == '+'))
            return 0;
    }
    return i > 0U && i < STORAGE_PACKAGE_ID_MAX;
}

firmware_status_t UpdateRequest_Validate(const storage_boot_update_request_t *request)
{
    if (request == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (request->format_version != BOOT_SUPPORTED_FORMAT_VERSION || request->requested == 0U ||
        request->component_mask == 0U || !package_id_valid(request->package_id) ||
        !UpdateRequest_IsManifestHashValid(request->manifest_sha256))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateRequest_Parse(const uint8_t *data, size_t size,
                                      storage_boot_update_request_t *request)
{
    cursor_t c = {data, size, 0U};
    char key[32];
    uint32_t seen = 0U;
    if (data == NULL || request == NULL || size == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(request, 0, sizeof(*request));
    if (!take(&c, '{'))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (;;)
    {
        uint32_t bit;
        skip_ws(&c);
        if (take(&c, '}'))
            break;
        if (!string_value(&c, key, sizeof(key)) || !take(&c, ':'))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        if (strcmp(key, "format_version") == 0)
        {
            bit = 1U;
            if ((seen & bit) || !uint_value(&c, &request->format_version))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
        }
        else if (strcmp(key, "requested") == 0)
        {
            bit = 2U;
            if ((seen & bit) || !bool_true(&c))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            request->requested = 1U;
        }
        else if (strcmp(key, "package_id") == 0)
        {
            bit = 4U;
            if ((seen & bit) || !string_value(&c, request->package_id, sizeof(request->package_id)))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
        }
        else if (strcmp(key, "manifest_sha256") == 0)
        {
            bit = 8U;
            if ((seen & bit) ||
                !string_value(&c, request->manifest_sha256, sizeof(request->manifest_sha256)))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
        }
        else if (strcmp(key, "component_mask") == 0)
        {
            bit = 16U;
            if ((seen & bit) || !uint_value(&c, &request->component_mask))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
        }
        else
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        seen |= bit;
        skip_ws(&c);
        if (take(&c, '}'))
            break;
        if (!take(&c, ','))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    skip_ws(&c);
    if (c.pos != c.size || seen != 31U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return UpdateRequest_Validate(request);
}
