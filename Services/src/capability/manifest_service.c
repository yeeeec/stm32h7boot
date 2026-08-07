/**
 * @file manifest_service.c
 * @brief Strict raw-bin-v1 Manifest validation and integrity hashing.
 */
#include "services/capability/manifest_service.h"

#include <stddef.h>
#include <string.h>

#define MANIFEST_FORMAT_VERSION 1U
#define APP_MAXIMUM_IMAGE_SIZE  1048576UL
#define GUI_MAXIMUM_IMAGE_SIZE  8388608UL

static firmware_status_t FindMember(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    return JsonDocument_FindMember(document, object, key, value);
}

static firmware_status_t ValidateObjectMembers(const json_document_t *document, uint32_t object,
                                               const char *const *names, uint32_t name_count)
{
    uint32_t index;
    uint32_t ignored;

    if ((object >= document->token_count) || (document->tokens[object].type != JSON_TOKEN_OBJECT) ||
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

static firmware_status_t RequireString(const json_document_t *document, uint32_t object,
                                       const char *key, uint32_t *token)
{
    firmware_status_t status = FindMember(document, object, key, token);

    if (!FirmwareStatus_IsOk(status) || (document->tokens[*token].type != JSON_TOKEN_STRING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequireConstantString(const json_document_t *document, uint32_t object,
                                               const char *key, const char *expected)
{
    uint32_t token;

    return (FirmwareStatus_IsOk(RequireString(document, object, key, &token)) &&
            JsonDocument_StringEquals(document, token, expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireU32(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    uint32_t token;
    firmware_status_t status = FindMember(document, object, key, &token);

    return FirmwareStatus_IsOk(status) ? JsonDocument_GetU32(document, token, value)
                                       : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireConstantU32(const json_document_t *document, uint32_t object,
                                            const char *key, uint32_t expected)
{
    uint32_t value;

    return (FirmwareStatus_IsOk(RequireU32(document, object, key, &value)) && (value == expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

static int TokenMatchesPattern(const json_document_t *document, uint32_t token_index,
                               uint32_t minimum, uint32_t maximum, const char *additional)
{
    const json_token_t *token = &document->tokens[token_index];
    uint32_t length           = token->end - token->start;
    uint32_t index;

    if ((token->type != JSON_TOKEN_STRING) || (length < minimum) || (length > maximum))
    {
        return 0;
    }
    for (index = token->start; index < token->end; ++index)
    {
        uint8_t value = document->data[index];

        if (!(((value >= 'A') && (value <= 'Z')) || ((value >= 'a') && (value <= 'z')) ||
              ((value >= '0') && (value <= '9')) || (strchr(additional, (int)value) != NULL)))
        {
            return 0;
        }
    }
    return 1;
}

static firmware_status_t ParseVersion(const json_document_t *document, uint32_t token_index,
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

        if ((position >= token->end) || (document->data[position] < '0') ||
            (document->data[position] > '9'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if ((document->data[position] == '0') && ((position + 1U) < token->end) &&
            (document->data[position + 1U] >= '0') && (document->data[position + 1U] <= '9'))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        while ((position < token->end) && (document->data[position] >= '0') &&
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

static int HexDigit(uint8_t value)
{
    if ((value >= '0') && (value <= '9'))
    {
        return value - '0';
    }
    if ((value >= 'a') && (value <= 'f'))
    {
        return value - 'a' + 10;
    }
    return -1;
}

static firmware_status_t ParseSha256(const json_document_t *document, uint32_t object,
                                     const char *key, uint8_t output[MANIFEST_SHA256_SIZE])
{
    uint32_t token;
    uint32_t index;

    if (!FirmwareStatus_IsOk(RequireString(document, object, key, &token)) ||
        ((document->tokens[token].end - document->tokens[token].start) !=
         MANIFEST_SHA256_SIZE * 2U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0U; index < MANIFEST_SHA256_SIZE; ++index)
    {
        int high = HexDigit(document->data[document->tokens[token].start + index * 2U]);
        int low = HexDigit(document->data[document->tokens[token].start + index * 2U + 1U]);

        if ((high < 0) || (low < 0))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        output[index] = (uint8_t)((high << 4) | low);
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseRelease(const json_document_t *document, uint32_t root,
                                      validated_manifest_t *manifest)
{
    static const char *const members[] = {"major", "minor", "patch", "build"};
    uint32_t object;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;

    if (!FirmwareStatus_IsOk(FindMember(document, root, "release", &object)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 4U)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "major", &major)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "minor", &minor)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "patch", &patch)) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "build", &manifest->build_number)) ||
        (major > UINT16_MAX) || (minor > UINT16_MAX) || (patch > UINT16_MAX))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    manifest->release_version.major = (uint16_t)major;
    manifest->release_version.minor = (uint16_t)minor;
    manifest->release_version.patch = (uint16_t)patch;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseTarget(const json_document_t *document, uint32_t root,
                                     validated_manifest_t *manifest)
{
    static const char *const members[] = {"product", "hardware", "minimum_bootloader_version"};
    uint32_t object;
    uint32_t version;

    if (!FirmwareStatus_IsOk(FindMember(document, root, "target", &object)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 3U)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "product", "HMI")) ||
        !FirmwareStatus_IsOk(
            RequireConstantString(document, object, "hardware", "STM32H743-W25Q256")) ||
        !FirmwareStatus_IsOk(RequireString(document, object, "minimum_bootloader_version", &version)) ||
        !FirmwareStatus_IsOk(ParseVersion(document, version, &manifest->minimum_bootloader_version)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseComponent(const json_document_t *document, uint32_t object,
                                        const char *file, uint32_t maximum_size,
                                        manifest_app_component_t *component)
{
    static const char *const members[] = {"file", "format", "size", "sha256"};

    if (!FirmwareStatus_IsOk(ValidateObjectMembers(document, object, members, 4U)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "file", file)) ||
        !FirmwareStatus_IsOk(RequireConstantString(document, object, "format", "raw-bin-v1")) ||
        !FirmwareStatus_IsOk(RequireU32(document, object, "size", &component->size_bytes)) ||
        (component->size_bytes == 0U) || (component->size_bytes > maximum_size) ||
        !FirmwareStatus_IsOk(ParseSha256(document, object, "sha256", component->sha256)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    (void)strcpy(component->file, file);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ParseComponents(const json_document_t *document, uint32_t root,
                                         validated_manifest_t *manifest)
{
    static const char *const members[] = {"app", "gui"};
    uint32_t components;
    uint32_t app;
    uint32_t gui;

    if (!FirmwareStatus_IsOk(FindMember(document, root, "components", &components)) ||
        !FirmwareStatus_IsOk(ValidateObjectMembers(document, components, members, 2U)) ||
        !FirmwareStatus_IsOk(FindMember(document, components, "app", &app)) ||
        !FirmwareStatus_IsOk(FindMember(document, components, "gui", &gui)) ||
        !FirmwareStatus_IsOk(ParseComponent(document, app, "hmi.app.bin", APP_MAXIMUM_IMAGE_SIZE,
                                             &manifest->app)) ||
        !FirmwareStatus_IsOk(ParseComponent(document, gui, "hmi.gui.bin", GUI_MAXIMUM_IMAGE_SIZE,
                                             &manifest->gui)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ValidateRoot(const json_document_t *document,
                                      validated_manifest_t *manifest)
{
    static const char *const members[] = {"format_version", "package_id", "release", "target",
                                          "components"};
    uint32_t package_id;

    if (!FirmwareStatus_IsOk(ValidateObjectMembers(document, 0U, members, 5U)) ||
        !FirmwareStatus_IsOk(
            RequireConstantU32(document, 0U, "format_version", MANIFEST_FORMAT_VERSION)) ||
        !FirmwareStatus_IsOk(RequireString(document, 0U, "package_id", &package_id)) ||
        !TokenMatchesPattern(document, package_id, 1U, MANIFEST_PACKAGE_ID_MAX_SIZE, "._+-") ||
        !FirmwareStatus_IsOk(JsonDocument_CopyString(document, package_id, manifest->package_id,
                                                     sizeof(manifest->package_id))))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashBytes(const hash_provider_t *hash, const void *data, size_t size,
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

firmware_status_t ManifestService_Init(manifest_service_t *service,
                                       const manifest_service_dependencies_t *dependencies)
{
    const hash_provider_t *hash;

    if ((service == NULL) || (dependencies == NULL) || (dependencies->hash == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    hash = dependencies->hash;
    if ((hash->context == NULL) || (hash->reset == NULL) || (hash->update == NULL) ||
        (hash->finish == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    service->hash        = hash;
    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ManifestService_ParseAndValidate(struct manifest_service *service,
                                                   const uint8_t *data, uint32_t size,
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
    status = JsonDocument_Parse(&document, data, size, implementation->tokens,
                                MANIFEST_SERVICE_TOKEN_CAPACITY);
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateRoot(&document, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ParseRelease(&document, 0U, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ParseTarget(&document, 0U, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ParseComponents(&document, 0U, &parsed);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = HashBytes(implementation->hash, data, size, parsed.manifest_sha256);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = HashBytes(implementation->hash, parsed.package_id, strlen(parsed.package_id),
                           package_digest);
    }
    if (FirmwareStatus_IsOk(status))
    {
        memcpy(parsed.package_id_hash128, package_digest, MANIFEST_PACKAGE_HASH_SIZE);
        *manifest = parsed;
    }
    return status;
}
