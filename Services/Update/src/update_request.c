#include "update/update_request.h"

#include <stdio.h>
#include <string.h>

#include "firmware/memory.h"
#include "platform/platform_storage.h"
#include "update_config.h"
#include "update_internal.h"

typedef struct
{
    const uint8_t *data;
    size_t length;
    size_t position;
} request_cursor_t;

static void skip_space(request_cursor_t *cursor)
{
    while (cursor->position < cursor->length)
    {
        uint8_t c = cursor->data[cursor->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        ++cursor->position;
    }
}

static int consume(request_cursor_t *cursor, uint8_t expected)
{
    skip_space(cursor);
    if (cursor->position >= cursor->length || cursor->data[cursor->position] != expected)
        return 0;
    ++cursor->position;
    return 1;
}

static int copy_string(request_cursor_t *cursor, char *destination, size_t capacity)
{
    size_t output = 0U;
    skip_space(cursor);
    if (capacity == 0U || cursor->position >= cursor->length ||
        cursor->data[cursor->position++] != '"')
        return 0;
    while (cursor->position < cursor->length)
    {
        uint8_t c = cursor->data[cursor->position++];
        if (c == '"')
        {
            destination[output] = '\0';
            return 1;
        }
        if (c < 0x20U || c >= 0x80U)
            return 0;
        if (c == '\\')
        {
            if (cursor->position >= cursor->length)
                return 0;
            c = cursor->data[cursor->position++];
            if (c != '"' && c != '\\' && c != '/')
                return 0;
        }
        if (output + 1U >= capacity)
            return 0;
        destination[output++] = (char) c;
    }
    return 0;
}

static int read_uint(request_cursor_t *cursor, uint32_t *value)
{
    uint64_t number = 0U;
    size_t digits = 0U;
    skip_space(cursor);
    if (cursor->position >= cursor->length)
        return 0;
    if (cursor->data[cursor->position] == '0')
    {
        ++cursor->position;
        digits = 1U;
        if (cursor->position < cursor->length && cursor->data[cursor->position] >= '0' &&
            cursor->data[cursor->position] <= '9')
            return 0;
    }
    else
    {
        while (cursor->position < cursor->length && cursor->data[cursor->position] >= '0' &&
               cursor->data[cursor->position] <= '9')
        {
            uint32_t digit = (uint32_t) (cursor->data[cursor->position++] - '0');
            if (number > (UINT32_MAX - digit) / 10U)
                return 0;
            number = number * 10U + digit;
            ++digits;
        }
    }
    if (digits == 0U || value == NULL)
        return 0;
    *value = (uint32_t) number;
    return 1;
}

static int read_bool_true(request_cursor_t *cursor)
{
    static const uint8_t literal[] = {'t', 'r', 'u', 'e'};
    size_t index;
    skip_space(cursor);
    if (cursor->length - cursor->position < sizeof(literal))
        return 0;
    for (index = 0U; index < sizeof(literal); ++index)
        if (cursor->data[cursor->position + index] != literal[index])
            return 0;
    cursor->position += sizeof(literal);
    return 1;
}

static int valid_digest(const char *digest)
{
    size_t index;
    if (digest == NULL || strlen(digest) != 64U)
        return 0;
    for (index = 0U; index < 64U; ++index)
    {
        char c = digest[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return 1;
}

firmware_status_t UpdateRequest_Parse(const uint8_t *json, size_t length,
                                      update_request_t *request)
{
    request_cursor_t cursor;
    uint32_t seen = 0U;
    char key[32];

    if (json == NULL || request == NULL || length == 0U || length > UPDATE_REQUEST_MAX_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(request, 0, sizeof(*request));
    cursor.data = json;
    cursor.length = length;
    cursor.position = 0U;
    if (!consume(&cursor, '{'))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (;;)
    {
        skip_space(&cursor);
        if (consume(&cursor, '}'))
            break;
        if (!copy_string(&cursor, key, sizeof(key)) || !consume(&cursor, ':'))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        if (strcmp(key, "format_version") == 0 && (seen & 1U) == 0U)
        {
            if (!read_uint(&cursor, &request->format_version) || request->format_version != 1U)
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 1U;
        }
        else if (strcmp(key, "requested") == 0 && (seen & 2U) == 0U)
        {
            skip_space(&cursor);
            if (read_bool_true(&cursor))
                request->requested = 1U;
            else
            {
                static const uint8_t false_literal[] = {'f', 'a', 'l', 's', 'e'};
                size_t index;
                if (cursor.length - cursor.position < sizeof(false_literal))
                    return FIRMWARE_STATUS_INVALID_ARGUMENT;
                for (index = 0U; index < sizeof(false_literal); ++index)
                    if (cursor.data[cursor.position + index] != false_literal[index])
                        return FIRMWARE_STATUS_INVALID_ARGUMENT;
                cursor.position += sizeof(false_literal);
                request->requested = 0U;
            }
            seen |= 2U;
        }
        else if (strcmp(key, "package_id") == 0 && (seen & 4U) == 0U)
        {
            if (!copy_string(&cursor, request->package_id, sizeof(request->package_id)) ||
                !UpdatePackage_IsValidId(request->package_id))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 4U;
        }
        else if (strcmp(key, "manifest_sha256") == 0 && (seen & 8U) == 0U)
        {
            if (!copy_string(&cursor, request->manifest_sha256,
                             sizeof(request->manifest_sha256)) ||
                !valid_digest(request->manifest_sha256))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 8U;
        }
        else if (strcmp(key, "component_mask") == 0 && (seen & 16U) == 0U)
        {
            if (!read_uint(&cursor, &request->component_mask))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 16U;
        }
        else
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        skip_space(&cursor);
        if (consume(&cursor, '}'))
            break;
        if (!consume(&cursor, ','))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    skip_space(&cursor);
    if (cursor.position != cursor.length || seen != 31U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return FIRMWARE_STATUS_OK;
}

update_request_presence_t UpdateRequest_Load(update_request_t *request)
{
    static FIRMWARE_STORAGE_RAM uint8_t buffer[UPDATE_REQUEST_MAX_SIZE];
    const char *path = UPDATE_REQUEST_PATH;
    platform_file_handle_t file;
    platform_file_info_t info;
    size_t total = 0U;
    firmware_status_t status;

    if (request == NULL)
        return UPDATE_REQUEST_INVALID;
    status = PlatformStorage_Stat(path, &info);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return UPDATE_REQUEST_ABSENT;
    if (FirmwareStatus_IsError(status) || info.is_directory != 0U || info.size == 0U ||
        info.size > sizeof(buffer))
        return UPDATE_REQUEST_INVALID;
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
        return UPDATE_REQUEST_INVALID;
    while (total < info.size)
    {
        size_t actual = 0U;
        status = PlatformStorage_Read(file, buffer + total, info.size - total, &actual);
        if (FirmwareStatus_IsError(status) || actual != info.size - total)
        {
            (void) PlatformStorage_Close(file);
            return UPDATE_REQUEST_INVALID;
        }
        total += actual;
    }
    status = PlatformStorage_Close(file);
    if (FirmwareStatus_IsError(status) || FirmwareStatus_IsError(UpdateRequest_Parse(buffer, total, request)))
        return UPDATE_REQUEST_INVALID;
    return request->requested != 0U ? UPDATE_REQUEST_ACTIVE : UPDATE_REQUEST_ABSENT;
}

firmware_status_t UpdateRequest_Write(const update_request_t *request)
{
    static FIRMWARE_STORAGE_RAM char buffer[UPDATE_REQUEST_MAX_SIZE];
    platform_file_handle_t file;
    size_t written;
    int length;
    firmware_status_t status;

    if (request == NULL || request->format_version != 1U || request->requested == 0U ||
        !UpdatePackage_IsValidId(request->package_id) || !valid_digest(request->manifest_sha256) ||
        (request->component_mask & ~31U) != 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    length = snprintf(buffer, sizeof(buffer),
                      "{\"format_version\":1,\"requested\":true,\"package_id\":\"%s\",\"manifest_sha256\":\"%s\",\"component_mask\":%lu}",
                      request->package_id, request->manifest_sha256,
                      (unsigned long) request->component_mask);
    if (length <= 0 || (size_t) length >= sizeof(buffer))
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    status = PlatformStorage_OpenWrite(UPDATE_REQUEST_PATH, &file);
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformStorage_Write(file, buffer, (size_t) length, &written);
    if (FirmwareStatus_IsOk(status) && written != (size_t) length)
        status = FIRMWARE_STATUS_IO_ERROR;
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Sync(file);
    {
        firmware_status_t close_status = PlatformStorage_Close(file);
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(close_status))
            status = close_status;
    }
    return status;
}

firmware_status_t UpdateRequest_Delete(void)
{
    firmware_status_t status = PlatformStorage_Remove(UPDATE_REQUEST_PATH);
    return status == FIRMWARE_STATUS_NOT_FOUND ? FIRMWARE_STATUS_OK : status;
}
