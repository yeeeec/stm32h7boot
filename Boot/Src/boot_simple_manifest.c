#include "boot_simple_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_platform.h"

static const char *Boot_SimpleManifest_SkipWhitespace(const char *cursor, const char *end) {
    while ((cursor < end) && isspace((unsigned char) *cursor)) {
        ++cursor;
    }

    return cursor;
}

static BootError Boot_SimpleManifest_ParseString(const char **cursor, const char *end, char *buffer,
                                                 size_t buffer_length) {
    const char *position;
    size_t length = 0U;

    if ((cursor == NULL) || (*cursor == NULL)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    position = Boot_SimpleManifest_SkipWhitespace(*cursor, end);
    if ((position >= end) || (*position != '"')) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    ++position;
    while (position < end) {
        char value = *position++;

        if (value == '"') {
            if ((buffer != NULL) && (buffer_length > 0U)) {
                buffer[length] = '\0';
            }
            *cursor = position;
            return BOOT_ERR_NONE;
        }

        if (value == '\\') {
            if (position >= end) {
                return BOOT_ERR_MANIFEST_PARSE;
            }

            value = *position++;
            switch (value) {
                case '"':
                case '\\':
                case '/':
                    break;

                case 'b':
                    value = '\b';
                    break;

                case 'f':
                    value = '\f';
                    break;

                case 'n':
                    value = '\n';
                    break;

                case 'r':
                    value = '\r';
                    break;

                case 't':
                    value = '\t';
                    break;

                default:
                    return BOOT_ERR_MANIFEST_PARSE;
            }
        }

        if ((buffer != NULL) && (buffer_length > 0U)) {
            if ((length + 1U) >= buffer_length) {
                return BOOT_ERR_MANIFEST_PARSE;
            }
            buffer[length] = value;
        }

        ++length;
    }

    return BOOT_ERR_MANIFEST_PARSE;
}

static BootError Boot_SimpleManifest_ParseU32Text(const char *text, uint32_t *value) {
    char *parse_end;
    unsigned long parsed_value;

    if ((text == NULL) || (value == NULL) || (*text == '\0')) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    parsed_value = strtoul(text, &parse_end, 0);
    if ((parse_end == text) || (*parse_end != '\0')) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    *value = (uint32_t) parsed_value;
    return BOOT_ERR_NONE;
}

static BootError Boot_SimpleManifest_ParseU32Value(const char **cursor, const char *end,
                                                   uint32_t *value) {
    const char *position;
    char token[32];
    size_t length = 0U;
    BootError error;

    if ((cursor == NULL) || (*cursor == NULL) || (value == NULL)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    position = Boot_SimpleManifest_SkipWhitespace(*cursor, end);
    if (position >= end) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    if (*position == '"') {
        error = Boot_SimpleManifest_ParseString(&position, end, token, sizeof(token));
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        *cursor = position;
        return Boot_SimpleManifest_ParseU32Text(token, value);
    }

    while ((position < end) && (*position != ',') && (*position != '}') && (*position != ']') &&
           !isspace((unsigned char) *position)) {
        if ((length + 1U) >= sizeof(token)) {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        token[length++] = *position++;
    }

    token[length] = '\0';
    *cursor       = position;
    return Boot_SimpleManifest_ParseU32Text(token, value);
}

static BootError Boot_SimpleManifest_SkipValue(const char **cursor, const char *end) {
    const char *position;
    uint32_t depth = 0U;
    char opening   = '\0';
    char closing   = '\0';
    int in_string  = 0;
    int escaped    = 0;

    if ((cursor == NULL) || (*cursor == NULL)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    position = Boot_SimpleManifest_SkipWhitespace(*cursor, end);
    if (position >= end) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    if (*position == '"') {
        BootError error = Boot_SimpleManifest_ParseString(&position, end, NULL, 0U);
        if (error == BOOT_ERR_NONE) {
            *cursor = position;
        }
        return error;
    }

    if ((*position != '{') && (*position != '[')) {
        while ((position < end) && (*position != ',') && (*position != '}') && (*position != ']')) {
            ++position;
        }

        *cursor = position;
        return BOOT_ERR_NONE;
    }

    opening = *position;
    closing = (opening == '{') ? '}' : ']';
    depth   = 1U;
    ++position;

    while ((position < end) && (depth > 0U)) {
        const char value = *position++;

        if (in_string != 0) {
            if (escaped != 0) {
                escaped = 0;
            } else if (value == '\\') {
                escaped = 1;
            } else if (value == '"') {
                in_string = 0;
            }

            continue;
        }

        if (value == '"') {
            in_string = 1;
            continue;
        }

        if (value == opening) {
            ++depth;
        } else if (value == closing) {
            --depth;
        }
    }

    if (depth != 0U) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    *cursor = position;
    return BOOT_ERR_NONE;
}

static BootError Boot_SimpleManifest_ParseOperation(const char *begin, const char *end,
                                                    BootManifestOperation *operation) {
    const char *cursor = begin;
    char relative_path[BOOT_FILE_PATH_LENGTH];
    BootError error;

    if ((begin == NULL) || (end == NULL) || (operation == NULL)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    memset(operation, 0, sizeof(*operation));

    while (cursor < end) {
        char key[24];
        cursor = Boot_SimpleManifest_SkipWhitespace(cursor, end);
        if (cursor >= end) {
            break;
        }

        if (*cursor == ',') {
            ++cursor;
            continue;
        }

        error = Boot_SimpleManifest_ParseString(&cursor, end, key, sizeof(key));
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        cursor = Boot_SimpleManifest_SkipWhitespace(cursor, end);
        if ((cursor >= end) || (*cursor != ':')) {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        ++cursor;
        if (strcmp(key, "file") == 0) {
            error = Boot_SimpleManifest_ParseString(&cursor, end, operation->file,
                                                    sizeof(operation->file));
        } else if (strcmp(key, "size") == 0) {
            error = Boot_SimpleManifest_ParseU32Value(&cursor, end, &operation->size);
        } else if (strcmp(key, "crc32") == 0) {
            error = Boot_SimpleManifest_ParseU32Value(&cursor, end, &operation->crc32);
        } else if (strcmp(key, "version") == 0) {
            error = Boot_SimpleManifest_ParseU32Value(&cursor, end, &operation->version);
        } else {
            error = Boot_SimpleManifest_SkipValue(&cursor, end);
        }

        if (error != BOOT_ERR_NONE) {
            return error;
        }

        cursor = Boot_SimpleManifest_SkipWhitespace(cursor, end);
        if ((cursor < end) && (*cursor == ',')) {
            ++cursor;
        }
    }

    if ((operation->file[0] == '\0') || (operation->size == 0U)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    error = Boot_SimpleManifest_BuildCrcPath(operation->file, relative_path, sizeof(relative_path));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    operation->present_in_crc_dir = Boot_Platform_FileExists(relative_path) ? 1U : 0U;
    return BOOT_ERR_NONE;
}

void Boot_SimpleManifest_Reset(BootManifest *manifest) {
    if (manifest != NULL) {
        memset(manifest, 0, sizeof(*manifest));
    }
}

BootError Boot_SimpleManifest_BuildCrcPath(const char *file_name, char *buffer,
                                           size_t buffer_length) {
    const char *name = file_name;
    int written_length;

    if ((file_name == NULL) || (buffer == NULL) || (buffer_length == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    while (*name == '/') {
        ++name;
    }

    written_length = snprintf(buffer, buffer_length, "%s/%s", BOOT_USB_CRC_DIR, name);
    if ((written_length <= 0) || ((size_t) written_length >= buffer_length)) {
        return BOOT_ERR_FILE_SIZE;
    }

    return BOOT_ERR_NONE;
}

BootError Boot_SimpleManifest_Load(BootManifest *manifest) {
    BootPlatformFile file;
    uint32_t bytes_read = 0U;
    uint32_t file_size;
    char buffer[BOOT_MANIFEST_FILE_MAX_SIZE + 1U];
    const char *cursor;
    const char *end;
    BootError error;

    if (manifest == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    Boot_SimpleManifest_Reset(manifest);

    error = Boot_Platform_FileOpenRead(BOOT_MANIFEST_PATH, &file);
    if (error == BOOT_ERR_FILE_MISSING) {
        return BOOT_ERR_MANIFEST_NOT_FOUND;
    }
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    file_size = Boot_Platform_FileSize(&file);
    if ((file_size == 0U) || (file_size > BOOT_MANIFEST_FILE_MAX_SIZE)) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_MANIFEST_PARSE;
    }

    error = Boot_Platform_FileRead(&file, buffer, file_size, &bytes_read);
    Boot_Platform_FileClose(&file);
    if ((error != BOOT_ERR_NONE) || (bytes_read != file_size)) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    buffer[file_size] = '\0';
    cursor            = strstr(buffer, "\"operations\"");
    if (cursor == NULL) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    end = buffer + file_size;
    while ((cursor < end) && (*cursor != '[')) {
        ++cursor;
    }
    if ((cursor >= end) || (*cursor != '[')) {
        return BOOT_ERR_MANIFEST_PARSE;
    }

    ++cursor;
    while (cursor < end) {
        const char *object_begin;
        const char *object_end;
        uint32_t depth = 0U;
        int in_string  = 0;
        int escaped    = 0;

        cursor = Boot_SimpleManifest_SkipWhitespace(cursor, end);
        if (cursor >= end) {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        if (*cursor == ']') {
            return BOOT_ERR_NONE;
        }

        if (*cursor == ',') {
            ++cursor;
            continue;
        }

        if (*cursor != '{') {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        if (manifest->count >= BOOT_MANIFEST_MAX_OPERATIONS) {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        object_begin = cursor + 1;
        depth        = 1U;
        ++cursor;
        while ((cursor < end) && (depth > 0U)) {
            const char value = *cursor++;

            if (in_string != 0) {
                if (escaped != 0) {
                    escaped = 0;
                } else if (value == '\\') {
                    escaped = 1;
                } else if (value == '"') {
                    in_string = 0;
                }

                continue;
            }

            if (value == '"') {
                in_string = 1;
            } else if (value == '{') {
                ++depth;
            } else if (value == '}') {
                --depth;
            }
        }

        if (depth != 0U) {
            return BOOT_ERR_MANIFEST_PARSE;
        }

        object_end = cursor - 1;
        error      = Boot_SimpleManifest_ParseOperation(object_begin, object_end,
                                                        &manifest->operations[manifest->count]);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        ++manifest->count;
    }

    return BOOT_ERR_MANIFEST_PARSE;
}

const BootManifestOperation *Boot_SimpleManifest_FindFirstSupported(const BootManifest *manifest) {
    uint32_t index;

    if (manifest == NULL) {
        return NULL;
    }

    for (index = 0U; index < manifest->count; ++index) {
        const BootManifestOperation *operation = &manifest->operations[index];

        if ((operation->present_in_crc_dir != 0U) && (strcmp(operation->file, "app.bin") == 0)) {
            return operation;
        }
    }

    return NULL;
}
