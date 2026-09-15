#include "storage/storage_common.h"

#include <stdio.h>
#include <string.h>

int Storage_IsSafePath(const char *path)
{
    const char *cursor;
    const char *segment;
    size_t length;

    if ((path == NULL) || (path[0] != '/') || (path[1] == '\0'))
        return 0;
    length = strlen(path);
    if ((length >= STORAGE_PATH_MAX) || (path[length - 1U] == '/'))
        return 0;
    segment = path + 1;
    for (cursor = segment;; ++cursor)
    {
        if ((*cursor == '/') || (*cursor == '\0'))
        {
            size_t segment_length = (size_t) (cursor - segment);
            if ((segment_length == 0U) ||
                ((segment_length == 1U) && (segment[0] == '.')) ||
                ((segment_length == 2U) && (segment[0] == '.') && (segment[1] == '.')))
                return 0;
            if (*cursor == '\0')
                break;
            segment = cursor + 1;
            continue;
        }
        {
            const unsigned char c = (unsigned char) *cursor;
            if ((c < 0x21U) || (c == '\\') || (c == '"') || (c == ':') || (c == '*') ||
                (c == '?'))
                return 0;
        }
    }
    return 1;
}

static int IsPackageToken(const char *value, size_t capacity)
{
    size_t i;

    if ((value == NULL) || (value[0] == '\0'))
        return 0;
    for (i = 0U; (i < capacity) && (value[i] != '\0'); ++i)
    {
        const unsigned char c = (unsigned char) value[i];
        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '-') || (c == '_') || (c == '.') ||
              (c == '+')))
            return 0;
    }
    return (i < capacity) && (value[i] == '\0');
}

static int IsSha256Hex(const char *value)
{
    size_t i;

    if ((value == NULL) || (strlen(value) != STORAGE_SHA256_HEX_LENGTH))
        return 0;
    for (i = 0U; i < STORAGE_SHA256_HEX_LENGTH; ++i)
    {
        const unsigned char c = (unsigned char) value[i];
        if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
              ((c >= 'A') && (c <= 'F'))))
            return 0;
    }
    return 1;
}

size_t Storage_FormatBootUpdateRequest(const storage_boot_update_request_t *request,
                                       char *buffer, size_t capacity)
{
    int result;

    if ((request == NULL) || (buffer == NULL) || (capacity == 0U) ||
        (request->format_version != 1U) || (request->requested > 1U) ||
        (request->component_mask == 0U) ||
        !IsPackageToken(request->package_id, sizeof(request->package_id)) ||
        !IsSha256Hex(request->manifest_sha256))
        return 0U;
    result = snprintf(buffer, capacity,
                      "{\n"
                      "  \"format_version\": %lu,\n"
                      "  \"requested\": %s,\n"
                      "  \"package_id\": \"%s\",\n"
                      "  \"manifest_sha256\": \"%s\",\n"
                      "  \"component_mask\": %lu\n"
                      "}\n",
                      (unsigned long) request->format_version,
                      request->requested != 0U ? "true" : "false", request->package_id,
                      request->manifest_sha256, (unsigned long) request->component_mask);
    if ((result <= 0) || ((size_t) result >= capacity))
        return 0U;
    return (size_t) result;
}
