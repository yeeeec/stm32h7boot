/**
 * @file update_request_service.c
 * @brief Strict trusted update request parser and Manifest byte binding.
 */
#include "services/capability/update_request_service.h"

#include <stddef.h>
#include <string.h>

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
        if (!FirmwareStatus_IsOk(JsonDocument_FindMember(document, object, names[index], &ignored)))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t RequireU32(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    uint32_t token;

    return FirmwareStatus_IsOk(JsonDocument_FindMember(document, object, key, &token))
               ? JsonDocument_GetU32(document, token, value)
               : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t RequireString(const json_document_t *document, uint32_t object,
                                       const char *key, uint32_t *token)
{
    if (!FirmwareStatus_IsOk(JsonDocument_FindMember(document, object, key, token)) ||
        (document->tokens[*token].type != JSON_TOKEN_STRING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static int PackageIdIsValid(const json_document_t *document, uint32_t token)
{
    uint32_t index;
    uint32_t length = document->tokens[token].end - document->tokens[token].start;

    if ((length == 0U) || (length > UPDATE_REQUEST_PACKAGE_ID_MAX_SIZE))
    {
        return 0;
    }
    for (index = document->tokens[token].start; index < document->tokens[token].end; ++index)
    {
        uint8_t value = document->data[index];

        if (!(((value >= 'A') && (value <= 'Z')) || ((value >= 'a') && (value <= 'z')) ||
              ((value >= '0') && (value <= '9')) || (value == '.') || (value == '_') ||
              (value == '+') || (value == '-')))
        {
            return 0;
        }
    }
    return 1;
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

static firmware_status_t ParseManifestSha256(const json_document_t *document, uint32_t token,
                                              uint8_t digest[UPDATE_REQUEST_MANIFEST_HASH_SIZE])
{
    uint32_t index;

    if ((document->tokens[token].type != JSON_TOKEN_STRING) ||
        ((document->tokens[token].end - document->tokens[token].start) !=
         UPDATE_REQUEST_MANIFEST_HASH_SIZE * 2U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0U; index < UPDATE_REQUEST_MANIFEST_HASH_SIZE; ++index)
    {
        int high = HexDigit(document->data[document->tokens[token].start + index * 2U]);
        int low = HexDigit(document->data[document->tokens[token].start + index * 2U + 1U]);

        if ((high < 0) || (low < 0))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashBytes(const hash_provider_t *hash, const void *data, size_t size,
                                   uint8_t digest[UPDATE_REQUEST_MANIFEST_HASH_SIZE])
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

firmware_status_t UpdateRequestService_Init(
    update_request_service_t *service,
    const update_request_service_dependencies_t *dependencies)
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

firmware_status_t UpdateRequestService_ParseAndValidate(struct update_request_service *service,
                                                        const uint8_t *data, uint32_t size,
                                                        update_request_t *request)
{
    static const char *const members[] = {"format_version", "requested", "package_id",
                                          "manifest_sha256"};
    update_request_service_t *implementation = (update_request_service_t *)service;
    update_request_t parsed;
    json_document_t document;
    uint32_t requested;
    uint32_t package_id;
    uint32_t manifest_sha256;
    int requested_value;
    firmware_status_t status;

    if ((implementation == NULL) || (data == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (implementation->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((size == 0U) || (size > UPDATE_REQUEST_SERVICE_MAX_DOCUMENT_SIZE))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memset(&parsed, 0, sizeof(parsed));
    status = JsonDocument_Parse(&document, data, size, implementation->tokens,
                                UPDATE_REQUEST_SERVICE_TOKEN_CAPACITY);
    if (FirmwareStatus_IsOk(status))
    {
        status = ValidateObjectMembers(&document, 0U, members, 4U);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = RequireU32(&document, 0U, "format_version", &parsed.format_version);
    }
    if (FirmwareStatus_IsOk(status) && (parsed.format_version != UPDATE_REQUEST_FORMAT_VERSION))
    {
        status = FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = JsonDocument_FindMember(&document, 0U, "requested", &requested);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = JsonDocument_GetBoolean(&document, requested, &requested_value);
    }
    if (FirmwareStatus_IsOk(status) && (requested_value == 0))
    {
        status = FIRMWARE_STATUS_INVALID_STATE;
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = RequireString(&document, 0U, "package_id", &package_id);
    }
    if (FirmwareStatus_IsOk(status) && !PackageIdIsValid(&document, package_id))
    {
        status = FIRMWARE_STATUS_INVALID_STATE;
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = JsonDocument_CopyString(&document, package_id, parsed.package_id,
                                         sizeof(parsed.package_id));
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = RequireString(&document, 0U, "manifest_sha256", &manifest_sha256);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = ParseManifestSha256(&document, manifest_sha256, parsed.manifest_sha256);
    }
    if (FirmwareStatus_IsOk(status))
    {
        parsed.requested = 1U;
        *request = parsed;
    }
    return status;
}

firmware_status_t UpdateRequestService_ValidateManifestBinding(
    struct update_request_service *service,
    const update_request_t *request,
    const uint8_t *manifest_data,
    uint32_t manifest_size,
    const validated_manifest_t *manifest)
{
    update_request_service_t *implementation = (update_request_service_t *)service;
    uint8_t raw_manifest_sha256[UPDATE_REQUEST_MANIFEST_HASH_SIZE];
    firmware_status_t status;

    if ((implementation == NULL) || (request == NULL) || (manifest_data == NULL) ||
        (manifest == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (implementation->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((request->format_version != UPDATE_REQUEST_FORMAT_VERSION) || (request->requested == 0U) ||
        (manifest_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = HashBytes(implementation->hash, manifest_data, manifest_size, raw_manifest_sha256);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((memcmp(request->manifest_sha256, raw_manifest_sha256, sizeof(raw_manifest_sha256)) != 0) ||
        (memcmp(manifest->manifest_sha256, raw_manifest_sha256, sizeof(raw_manifest_sha256)) != 0) ||
        (strcmp(request->package_id, manifest->package_id) != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
