/**
 * @file manifest_service.c
 * @brief Strict production Manifest validation and integrity hashing.
 */
#include "services/capability/manifest_service.h"

#include <stddef.h>
#include <string.h>


#define MANIFEST_FORMAT_VERSION 2U
#define APP_MAXIMUM_IMAGE_SIZE 1048576UL
#define GUI_MAXIMUM_IMAGE_SIZE 8388608UL
#define APP_LINK_ADDRESS 0x90000000UL
#define APP_MAXIMUM_RELOCATIONS 4096U

static firmware_status_t FindMember(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint32_t *value)
{
    return JsonDocument_FindMember(document, object, key, value);
}

static firmware_status_t ValidateObjectMembers(
    const json_document_t *document,
    uint32_t object,
    const char *const *names,
    uint32_t name_count)
{
    uint32_t index;
    uint32_t ignored;

    if ((object >= document->token_count) ||
        (document->tokens[object].type != JSON_TOKEN_OBJECT) ||
        (document->tokens[object].child_count != name_count))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0U; index < name_count; ++index)
    {
        if (!FirmwareStatus_IsOk(FindMember(document, object, names[index], &ignored)))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequireString(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint32_t *token)
{
    firmware_status_t status = FindMember(document, object, key, token);

    if (!FirmwareStatus_IsOk(status) ||
        (document->tokens[*token].type != JSON_TOKEN_STRING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequireConstantString(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    const char *expected)
{
    uint32_t token;

    return (FirmwareStatus_IsOk(RequireString(document, object, key, &token)) &&
            JsonDocument_StringEquals(document, token, expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireU32(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint32_t *value)
{
    uint32_t token;
    firmware_status_t status = FindMember(document, object, key, &token);

    return FirmwareStatus_IsOk(status)
               ? JsonDocument_GetU32(document, token, value)
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireConstantU32(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint32_t expected)
{
    uint32_t value;

    return (FirmwareStatus_IsOk(RequireU32(document, object, key, &value)) &&
            (value == expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireConstantBoolean(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    int expected)
{
    uint32_t token;
    int value;

    return (FirmwareStatus_IsOk(FindMember(document, object, key, &token)) &&
            FirmwareStatus_IsOk(JsonDocument_GetBoolean(document, token, &value)) &&
            (value == expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static int TokenMatchesPattern(
    const json_document_t *document,
    uint32_t token_index,
    uint32_t minimum,
    uint32_t maximum,
    const char *additional)
{
    const json_token_t *token = &document->tokens[token_index];
    uint32_t length = token->end - token->start;
    uint32_t index;

    if ((token->type != JSON_TOKEN_STRING) ||
        (length < minimum) || (length > maximum))
    {
        return 0;
    }
    for (index = token->start; index < token->end; ++index)
    {
        uint8_t value = document->data[index];

        if (!(((value >= 'A') && (value <= 'Z')) ||
              ((value >= 'a') && (value <= 'z')) ||
              ((value >= '0') && (value <= '9')) ||
              (strchr(additional, (int)value) != NULL)))
        {
            return 0;
        }
    }
    return 1;
}

static firmware_status_t ParseVersion(
    const json_document_t *document,
    uint32_t token_index,
    release_version_t *version)
{
    const json_token_t *token = &document->tokens[token_index];
    uint16_t parts[3];
    uint32_t position = token->start;
    uint32_t part;

    if ((token->type != JSON_TOKEN_STRING) || (token->start == token->end))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (part = 0U; part < 3U; ++part)
    {
        uint32_t value = 0U;
        uint32_t digits = 0U;

        if ((position >= token->end) ||
            (document->data[position] < '0') ||
            (document->data[position] > '9'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((document->data[position] == '0') &&
            ((position + 1U) < token->end) &&
            (document->data[position + 1U] >= '0') &&
            (document->data[position + 1U] <= '9'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        while ((position < token->end) &&
               (document->data[position] >= '0') &&
               (document->data[position] <= '9'))
        {
            value = value * 10U + (uint32_t)(document->data[position] - '0');
            if ((value > UINT16_MAX) || (++digits > 5U))
            {
                return FIRMWARE_STATUS_OUT_OF_RANGE;
            }
            ++position;
        }
        parts[part] = (uint16_t)value;
        if (part < 2U)
        {
            if ((position >= token->end) || (document->data[position++] != '.'))
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
        }
    }
    if (position != token->end)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    version->major = parts[0];
    version->minor = parts[1];
    version->patch = parts[2];
    return FIRMWARE_STATUS_OK;
}

static int IsLeapYear(uint32_t year)
{
    return ((year % 4U) == 0U) &&
           (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint32_t ParseTwoDigits(const uint8_t *data)
{
    return (uint32_t)(data[0] - '0') * 10U + (uint32_t)(data[1] - '0');
}

static int IsValidUtcTimestamp(
    const json_document_t *document,
    uint32_t token_index)
{
    static const uint8_t separators[6] = {'-', '-', 'T', ':', ':', 'Z'};
    static const uint8_t separator_positions[6] = {4U, 7U, 10U, 13U, 16U, 19U};
    static const uint8_t days_per_month[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    const json_token_t *token = &document->tokens[token_index];
    const uint8_t *data = &document->data[token->start];
    uint32_t index;
    uint32_t year = 0U;
    uint32_t month;
    uint32_t day;
    uint32_t maximum_day;

    if ((token->type != JSON_TOKEN_STRING) ||
        ((token->end - token->start) != 20U))
    {
        return 0;
    }
    for (index = 0U; index < 20U; ++index)
    {
        if ((index == 4U) || (index == 7U) || (index == 10U) ||
            (index == 13U) || (index == 16U) || (index == 19U))
        {
            continue;
        }
        if ((data[index] < '0') || (data[index] > '9'))
        {
            return 0;
        }
    }
    for (index = 0U; index < 6U; ++index)
    {
        if (data[separator_positions[index]] != separators[index])
        {
            return 0;
        }
    }
    for (index = 0U; index < 4U; ++index)
    {
        year = year * 10U + (uint32_t)(data[index] - '0');
    }
    month = ParseTwoDigits(&data[5]);
    day = ParseTwoDigits(&data[8]);
    if ((year == 0U) || (month == 0U) || (month > 12U))
    {
        return 0;
    }
    maximum_day = days_per_month[month - 1U];
    if ((month == 2U) && IsLeapYear(year))
    {
        ++maximum_day;
    }
    return (day >= 1U) && (day <= maximum_day) &&
           (ParseTwoDigits(&data[11]) <= 23U) &&
           (ParseTwoDigits(&data[14]) <= 59U) &&
           (ParseTwoDigits(&data[17]) <= 59U);
}

static int HexDigit(uint8_t value, int lowercase_only)
{
    if ((value >= '0') && (value <= '9'))
    {
        return value - '0';
    }
    if (lowercase_only && (value >= 'a') && (value <= 'f'))
    {
        return value - 'a' + 10;
    }
    if (!lowercase_only && (value >= 'A') && (value <= 'F'))
    {
        return value - 'A' + 10;
    }
    return -1;
}

static firmware_status_t ParseHex(
    const json_document_t *document,
    uint32_t token_index,
    uint8_t *output,
    uint32_t output_size,
    int lowercase_only)
{
    const json_token_t *token = &document->tokens[token_index];
    uint32_t index;

    if ((token->type != JSON_TOKEN_STRING) ||
        ((token->end - token->start) != output_size * 2U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0U; index < output_size; ++index)
    {
        int high = HexDigit(document->data[token->start + index * 2U], lowercase_only);
        int low = HexDigit(document->data[token->start + index * 2U + 1U], lowercase_only);

        if ((high < 0) || (low < 0))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        output[index] = (uint8_t)((high << 4) | low);
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseCrc32(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint32_t *value)
{
    uint32_t token;
    uint8_t bytes[4];
    firmware_status_t status = RequireString(document, object, key, &token);

    if (FirmwareStatus_IsOk(status))
    {
        status = ParseHex(document, token, bytes, sizeof(bytes), 0);
    }
    if (FirmwareStatus_IsOk(status))
    {
        *value = ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
                 ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
    }
    return status;
}

static firmware_status_t ParseSha256(
    const json_document_t *document,
    uint32_t object,
    const char *key,
    uint8_t output[MANIFEST_SHA256_SIZE])
{
    uint32_t token;
    firmware_status_t status = RequireString(document, object, key, &token);

    return FirmwareStatus_IsOk(status)
               ? ParseHex(document, token, output, MANIFEST_SHA256_SIZE, 1)
               : status;
}

static firmware_status_t ValidateReleaseGroup(
    const json_document_t *document,
    uint32_t root,
    validated_manifest_t *manifest)
{
    static const char *const members[] = {"id", "version", "atomic", "components"};
    uint32_t array;
    uint32_t group;
    uint32_t version;
    uint32_t components;
    uint32_t app;
    uint32_t gui;

    if (!FirmwareStatus_IsOk(FindMember(document, root, "release_groups", &array)) ||
        (document->tokens[array].type != JSON_TOKEN_ARRAY) ||
        (document->tokens[array].child_count != 1U) ||
        !FirmwareStatus_IsOk(JsonDocument_ArrayGet(document, array, 0U, &group)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, group, members, 4U)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, group, "id", "app-gui")) ||
        !FirmwareStatus_IsOk(RequireConstantBoolean(document, group, "atomic", 1)) ||
        !FirmwareStatus_IsOk(RequireString(document, group, "version", &version)) ||
        !FirmwareStatus_IsOk(ParseVersion(document, version, &manifest->release_version)) ||
        !FirmwareStatus_IsOk(FindMember(document, group, "components", &components)) ||
        (document->tokens[components].type != JSON_TOKEN_ARRAY) ||
        (document->tokens[components].child_count != 2U) ||
        !FirmwareStatus_IsOk(JsonDocument_ArrayGet(document, components, 0U, &app)) ||
        !JsonDocument_StringEquals(document, app, "app") ||
        !FirmwareStatus_IsOk(JsonDocument_ArrayGet(document, components, 1U, &gui)) ||
        !JsonDocument_StringEquals(document, gui, "gui"))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ValidateTransaction(
    const json_document_t *document,
    uint32_t root)
{
    static const char *const members[] = {
        "release_group_id", "strategy", "commit_store", "commit_condition"};
    uint32_t object;

    return (FirmwareStatus_IsOk(FindMember(document, root, "transaction", &object)) &&
            FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 4U)) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "release_group_id", "app-gui")) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "strategy", "inactive-pair")) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "commit_store", "eeprom")) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "commit_condition", "all-components-crc-valid")))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t ValidateCrcParameters(
    const json_document_t *document,
    uint32_t root)
{
    static const char *const members[] = {
        "name", "polynomial", "initial_value", "reflect_input",
        "reflect_output", "xor_output"};
    uint32_t object;

    return (FirmwareStatus_IsOk(FindMember(document, root, "crc32_parameters", &object)) &&
            FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 6U)) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "name", "CRC-32/ISO-HDLC")) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "polynomial", "0x04C11DB7")) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "initial_value", "0xFFFFFFFF")) &&
            FirmwareStatus_IsOk(RequireConstantBoolean(document, object, "reflect_input", 1)) &&
            FirmwareStatus_IsOk(RequireConstantBoolean(document, object, "reflect_output", 1)) &&
            FirmwareStatus_IsOk(RequireConstantString(document, object, "xor_output", "0xFFFFFFFF")))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t ParseAppComponent(
    const json_document_t *document,
    uint32_t object,
    manifest_app_component_t *app)
{
    static const char *const members[] = {
        "id", "file", "format", "target", "maximum_image_size_bytes",
        "file_size_bytes", "image_size_bytes", "source_crc32", "target_crc32",
        "sha256", "vector_offset", "entry_offset", "link_address", "relocation"};
    static const char *const target_members[] = {"app1", "app2"};
    static const char *const relocation_members[] = {"file", "count", "crc32", "format"};
    uint32_t target;
    uint32_t relocation;
    uint32_t sha;
    uint32_t relocation_file;

    if (!FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 14U)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "id", "app")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "file", "hmi.app.bin")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "format", "raw-xip-reloc-v2")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "target", "inactive-app-slot")) ||
        !FirmwareStatus_IsOk(RequireConstantU32(document, object, "maximum_image_size_bytes", APP_MAXIMUM_IMAGE_SIZE)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "file_size_bytes", &app->file_size_bytes)) ||
        (app->file_size_bytes == 0U) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "image_size_bytes", &app->image_size_bytes)) ||
        (app->image_size_bytes == 0U) || (app->image_size_bytes > APP_MAXIMUM_IMAGE_SIZE) ||
        (app->file_size_bytes != app->image_size_bytes) ||
        !FirmwareStatus_IsOk(ParseCrc32(document, object, "source_crc32", &app->source_crc32)) ||
        !FirmwareStatus_IsOk(FindMember(document, object, "target_crc32", &target)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, target, target_members, 2U)) ||
        !FirmwareStatus_IsOk(ParseCrc32(document, target, "app1", &app->target_crc32_app1)) ||
        !FirmwareStatus_IsOk(ParseCrc32(document, target, "app2", &app->target_crc32_app2)) ||
        !FirmwareStatus_IsOk(RequireString(document, object, "sha256", &sha)) ||
        !FirmwareStatus_IsOk(ParseHex(document, sha, app->sha256, MANIFEST_SHA256_SIZE, 1)) ||
        !FirmwareStatus_IsOk(RequireConstantU32(document, object, "vector_offset", 0U)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "entry_offset", &app->entry_offset)) ||
        (app->entry_offset >= app->image_size_bytes) || ((app->entry_offset & 1U) != 0U) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "link_address", &app->link_address)) ||
        (app->link_address != APP_LINK_ADDRESS) ||
        !FirmwareStatus_IsOk(FindMember(document, object, "relocation", &relocation)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, relocation, relocation_members, 4U)) ||
        !FirmwareStatus_IsOk(RequireString(document, relocation, "file", &relocation_file)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, relocation, "file", "hmi.app.reloc.bin")) ||
        !TokenMatchesPattern(document, relocation_file, 1U, MANIFEST_FILE_NAME_MAX_SIZE, "._+-") ||
        !FirmwareStatus_IsOk(JsonDocument_CopyString(
            document, relocation_file, app->relocation_file, sizeof(app->relocation_file))) ||
        !FirmwareStatus_IsOk(RequireU32(document, relocation, "count", &app->relocation_count)) ||
        (app->relocation_count == 0U) ||
        (app->relocation_count > APP_MAXIMUM_RELOCATIONS) ||
        !FirmwareStatus_IsOk(ParseCrc32(document, relocation, "crc32", &app->relocation_crc32)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, relocation, "format", "hmi-reloc-v1")))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    (void)strcpy(app->file, "hmi.app.bin");
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseGuiComponent(
    const json_document_t *document,
    uint32_t object,
    manifest_gui_component_t *gui)
{
    static const char *const members[] = {
        "id", "file", "format", "target", "maximum_image_size_bytes",
        "file_size_bytes", "crc32", "sha256"};

    if (!FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 8U)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "id", "gui")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "file", "hmi.gui.bin")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "format", "raw")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "target", "inactive-gui-slot")) ||
        !FirmwareStatus_IsOk(RequireConstantU32(document, object, "maximum_image_size_bytes", GUI_MAXIMUM_IMAGE_SIZE)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "file_size_bytes", &gui->file_size_bytes)) ||
        (gui->file_size_bytes == 0U) || (gui->file_size_bytes > GUI_MAXIMUM_IMAGE_SIZE) ||
        !FirmwareStatus_IsOk(ParseCrc32(document, object, "crc32", &gui->crc32)) ||
        !FirmwareStatus_IsOk(ParseSha256(document, object, "sha256", gui->sha256)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    (void)strcpy(gui->file, "hmi.gui.bin");
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseComponents(
    const json_document_t *document,
    uint32_t root,
    validated_manifest_t *manifest)
{
    uint32_t components;
    uint32_t app;
    uint32_t gui;

    if (!FirmwareStatus_IsOk(FindMember(document, root, "components", &components)) ||
        (document->tokens[components].type != JSON_TOKEN_ARRAY) ||
        (document->tokens[components].child_count != 2U) ||
        !FirmwareStatus_IsOk(JsonDocument_ArrayGet(document, components, 0U, &app)) ||
        !FirmwareStatus_IsOk(JsonDocument_ArrayGet(document, components, 1U, &gui)) ||
        !FirmwareStatus_IsOk(ParseAppComponent(document, app, &manifest->app)) ||
        !FirmwareStatus_IsOk(ParseGuiComponent(document, gui, &manifest->gui)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ValidateRoot(
    const json_document_t *document,
    validated_manifest_t *manifest)
{
    static const char *const members[] = {
        "format_version", "package_id", "product_id", "hardware_id",
        "build_number", "created_utc", "minimum_bootloader_version",
        "release_groups", "transaction", "crc32_parameters", "components",
        "signature"};
    uint32_t package_id;
    uint32_t created_utc;
    uint32_t version;

    if (!FirmwareStatus_IsOk(ValidateObjectMembers(document, 0U, members, 12U)) ||
        !FirmwareStatus_IsOk(RequireConstantU32(document, 0U, "format_version", MANIFEST_FORMAT_VERSION)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, 0U, "product_id", "HMI")) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, 0U, "hardware_id", "STM32H743-W25Q256")) ||
        !FirmwareStatus_IsOk(RequireString(document, 0U, "package_id", &package_id)) ||
        !TokenMatchesPattern(document, package_id, 1U, MANIFEST_PACKAGE_ID_MAX_SIZE, "._+-") ||
        !FirmwareStatus_IsOk(JsonDocument_CopyString(document, package_id, manifest->package_id, sizeof(manifest->package_id))) ||
        !FirmwareStatus_IsOk(RequireU32(document, 0U, "build_number", &manifest->build_number)) ||
        !FirmwareStatus_IsOk(RequireString(document, 0U, "created_utc", &created_utc)) ||
        !IsValidUtcTimestamp(document, created_utc) ||
        !FirmwareStatus_IsOk(RequireString(document, 0U, "minimum_bootloader_version", &version)) ||
        !FirmwareStatus_IsOk(ParseVersion(document, version, &manifest->minimum_bootloader_version)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashBytes(
    const hash_provider_t *hash,
    const void *data,
    size_t size,
    uint8_t digest[MANIFEST_SHA256_SIZE])
{
    firmware_status_t status = hash->reset(hash->context);

    if (FirmwareStatus_IsOk(status))
    {
        status = hash->update(hash->context, data, size);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = hash->finish(hash->context, digest);
    }
    return status;
}

firmware_status_t ManifestService_Init(
    manifest_service_t *service,
    const manifest_service_dependencies_t *dependencies)
{
    const hash_provider_t *hash;

    if ((service == NULL) || (dependencies == NULL) ||
        (dependencies->hash == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    hash = dependencies->hash;
    if ((hash->context == NULL) || (hash->reset == NULL) ||
        (hash->update == NULL) || (hash->finish == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    service->hash = hash;
    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ManifestService_ParseAndValidate(
    struct manifest_service *service,
    const uint8_t *data,
    uint32_t size,
    validated_manifest_t *manifest)
{
    manifest_service_t *implementation = (manifest_service_t *)service;
    validated_manifest_t parsed;
    json_document_t document;
    uint8_t package_digest[MANIFEST_SHA256_SIZE];
    firmware_status_t status;

    if ((implementation == NULL) || (data == NULL) || (manifest == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (implementation->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((size == 0U) || (size > MANIFEST_SERVICE_MAX_DOCUMENT_SIZE))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memset(&parsed, 0, sizeof(parsed));
    status = JsonDocument_Parse(
        &document, data, size, implementation->tokens,
        MANIFEST_SERVICE_TOKEN_CAPACITY);
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateRoot(&document, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateReleaseGroup(&document, 0U, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateTransaction(&document, 0U);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateCrcParameters(&document, 0U);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ParseComponents(&document, 0U, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = HashBytes(
            implementation->hash, data, size, parsed.manifest_sha256);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = HashBytes(
            implementation->hash,
            parsed.package_id,
            strlen(parsed.package_id),
            package_digest);
    }
    if (FirmwareStatus_IsOk(status))
    {
        memcpy(parsed.package_id_hash128, package_digest, MANIFEST_PACKAGE_HASH_SIZE);
    }
    if (FirmwareStatus_IsOk(status))
    {
        *manifest = parsed;
    }
    return status;
}
