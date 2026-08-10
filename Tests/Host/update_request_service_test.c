#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "services/capability/manifest_service.h"
#include "services/capability/update_request_service.h"

#define ASSERT_TRUE(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            printf("assertion failed at line %d\n", __LINE__);                  \
            return 1;                                                            \
        }                                                                        \
    } while (0)

static const char manifest_bytes[] =
    "{"
    "\"format_version\":1,"
    "\"package_id\":\"hmi-app-gui-1.2.3+42\","
    "\"release\":{\"major\":1,\"minor\":2,\"patch\":3,\"build\":42},"
    "\"target\":{\"product\":\"HMI\",\"hardware\":\"STM32H743-W25Q256\","
    "\"minimum_bootloader_version\":\"1.0.0\"},"
    "\"components\":{"
        "\"app\":{\"file\":\"hmi.app.bin\",\"format\":\"raw-bin-v1\","
        "\"size\":64,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"},"
        "\"gui\":{\"file\":\"hmi.gui.bin\",\"format\":\"raw-bin-v1\","
        "\"size\":128,\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\"}"
    "}"
    "}";

typedef struct
{
    hash_provider_t interface;
    uint32_t hash;
} fake_hash_t;

static firmware_status_t HashReset(void *context)
{
    fake_hash_t *fake = (fake_hash_t *)context;

    fake->hash = 2166136261UL;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashUpdate(void *context, const void *data, size_t size)
{
    fake_hash_t *fake = (fake_hash_t *)context;
    const uint8_t *bytes = (const uint8_t *)data;

    while (size-- != 0U)
    {
        fake->hash = (fake->hash ^ *bytes++) * 16777619UL;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashFinish(void *context, uint8_t digest[32])
{
    const fake_hash_t *fake = (const fake_hash_t *)context;
    uint32_t index;

    for (index = 0U; index < 32U; ++index)
    {
        digest[index] = (uint8_t)(fake->hash >> ((index & 3U) * 8U));
    }
    return FIRMWARE_STATUS_OK;
}

static void FakeHash_Init(fake_hash_t *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->interface.context = fake;
    fake->interface.reset = HashReset;
    fake->interface.update = HashUpdate;
    fake->interface.finish = HashFinish;
}

static void HashManifest(fake_hash_t *fake, uint8_t digest[32])
{
    (void)HashReset(fake);
    (void)HashUpdate(fake, manifest_bytes, strlen(manifest_bytes));
    (void)HashFinish(fake, digest);
}

static void HexEncode(const uint8_t digest[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    uint32_t index;

    for (index = 0U; index < 32U; ++index)
    {
        output[index * 2U] = digits[digest[index] >> 4U];
        output[index * 2U + 1U] = digits[digest[index] & 0x0FU];
    }
    output[64] = '\0';
}

int main(void)
{
    fake_hash_t fake;
    manifest_service_t manifest_service = {0};
    update_request_service_t request_service = {0};
    manifest_service_dependencies_t manifest_dependencies;
    update_request_service_dependencies_t request_dependencies;
    validated_manifest_t manifest;
    update_request_t request;
    update_request_t unchanged_request;
    uint8_t manifest_digest[32];
    char manifest_hex[65];
    char request_json[256];
    char invalid_request[sizeof(request_json)];
    char changed_manifest[sizeof(manifest_bytes)];
    char original_first_hex;
    char *field;
    int written;

    FakeHash_Init(&fake);
    manifest_dependencies.hash = &fake.interface;
    request_dependencies.hash = &fake.interface;
    ASSERT_TRUE(ManifestService_Init(&manifest_service, &manifest_dependencies) ==
                FIRMWARE_STATUS_OK);
    ASSERT_TRUE(UpdateRequestService_Init(&request_service, &request_dependencies) ==
                FIRMWARE_STATUS_OK);
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &manifest_service, (const uint8_t *)manifest_bytes,
                    (uint32_t)strlen(manifest_bytes), &manifest) == FIRMWARE_STATUS_OK);

    HashManifest(&fake, manifest_digest);
    HexEncode(manifest_digest, manifest_hex);
    written = snprintf(request_json, sizeof(request_json),
                       "{\"format_version\":1,\"requested\":true,"
                       "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                       "\"manifest_sha256\":\"%s\"}",
                       manifest_hex);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(request_json)));

    ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                    &request_service, (const uint8_t *)request_json, (uint32_t)written,
                    &request) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE((request.format_version == UPDATE_REQUEST_FORMAT_VERSION) &&
                (request.requested == 1U) &&
                (request.component_mask == (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI)) &&
                (strcmp(request.package_id, manifest.package_id) == 0));
    ASSERT_TRUE(UpdateRequestService_ValidateManifestBinding(
                    &request_service, &request, (const uint8_t *)manifest_bytes,
                    (uint32_t)strlen(manifest_bytes), &manifest) == FIRMWARE_STATUS_OK);

    /* Explicit masks accept every non-empty selection, while the legacy Manifest
     * correctly rejects a request for a component it does not declare. */
    {
        uint32_t mask;
        for (mask = UPDATE_COMPONENT_APP; mask <= UPDATE_COMPONENT_ALL; ++mask)
        {
            written = snprintf(request_json, sizeof(request_json),
                               "{\"format_version\":1,\"requested\":true,"
                               "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                               "\"manifest_sha256\":\"%s\",\"component_mask\":%lu}",
                               manifest_hex, (unsigned long)mask);
            ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(request_json)));
            ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                            &request_service, (const uint8_t *)request_json,
                            (uint32_t)written, &request) == FIRMWARE_STATUS_OK);
            ASSERT_TRUE(request.component_mask == mask);
        }
        written = snprintf(invalid_request, sizeof(invalid_request),
                           "{\"format_version\":1,\"requested\":true,"
                           "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                           "\"manifest_sha256\":\"%s\",\"component_mask\":0}",
                           manifest_hex);
        ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                        &request_service, (const uint8_t *)invalid_request,
                        (uint32_t)written, &request) != FIRMWARE_STATUS_OK);
        written = snprintf(invalid_request, sizeof(invalid_request),
                           "{\"format_version\":1,\"requested\":true,"
                           "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                           "\"manifest_sha256\":\"%s\",\"component_mask\":8}",
                           manifest_hex);
        ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                        &request_service, (const uint8_t *)invalid_request,
                        (uint32_t)written, &request) != FIRMWARE_STATUS_OK);
        request.component_mask = UPDATE_COMPONENT_THERAPY;
        ASSERT_TRUE(UpdateRequestService_ValidateManifestBinding(
                        &request_service, &request, (const uint8_t *)manifest_bytes,
                        (uint32_t)strlen(manifest_bytes), &manifest) != FIRMWARE_STATUS_OK);
    }

    request.manifest_sha256[0] ^= 0x01U;
    ASSERT_TRUE(UpdateRequestService_ValidateManifestBinding(
                    &request_service, &request, (const uint8_t *)manifest_bytes,
                    (uint32_t)strlen(manifest_bytes), &manifest) != FIRMWARE_STATUS_OK);
    request.manifest_sha256[0] ^= 0x01U;

    memcpy(changed_manifest, manifest_bytes, sizeof(manifest_bytes));
    field = strstr(changed_manifest, "\"build\":42");
    ASSERT_TRUE(field != NULL);
    field[strlen("\"build\":4")] = '3';
    ASSERT_TRUE(UpdateRequestService_ValidateManifestBinding(
                    &request_service, &request, (const uint8_t *)changed_manifest,
                    (uint32_t)strlen(changed_manifest), &manifest) != FIRMWARE_STATUS_OK);

    strcpy(request.package_id, "hmi-app-gui-1.2.3+43");
    ASSERT_TRUE(UpdateRequestService_ValidateManifestBinding(
                    &request_service, &request, (const uint8_t *)manifest_bytes,
                    (uint32_t)strlen(manifest_bytes), &manifest) != FIRMWARE_STATUS_OK);
    strcpy(request.package_id, manifest.package_id);

    written = snprintf(invalid_request, sizeof(invalid_request),
                       "{\"format_version\":2,\"requested\":true,"
                       "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                       "\"manifest_sha256\":\"%s\"}",
                       manifest_hex);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(invalid_request)));
    ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                    &request_service, (const uint8_t *)invalid_request, (uint32_t)written,
                    &request) == FIRMWARE_STATUS_NOT_SUPPORTED);

    written = snprintf(invalid_request, sizeof(invalid_request),
                       "{\"format_version\":1,\"requested\":false,"
                       "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                       "\"manifest_sha256\":\"%s\"}",
                       manifest_hex);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(invalid_request)));
    unchanged_request = request;
    ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                    &request_service, (const uint8_t *)invalid_request, (uint32_t)written,
                    &request) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&request, &unchanged_request, sizeof(request)) == 0);

    original_first_hex = manifest_hex[0];
    manifest_hex[0] = 'A';
    written = snprintf(invalid_request, sizeof(invalid_request),
                       "{\"format_version\":1,\"requested\":true,"
                       "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                       "\"manifest_sha256\":\"%s\"}",
                       manifest_hex);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(invalid_request)));
    ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                    &request_service, (const uint8_t *)invalid_request, (uint32_t)written,
                    &request) != FIRMWARE_STATUS_OK);
    manifest_hex[0] = original_first_hex;

    written = snprintf(invalid_request, sizeof(invalid_request),
                       "{\"format_version\":1,\"requested\":true,"
                       "\"package_id\":\"hmi-app-gui-1.2.3+42\","
                       "\"manifest_sha256\":\"%s\",\"extra\":0}",
                       manifest_hex);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(invalid_request)));
    ASSERT_TRUE(UpdateRequestService_ParseAndValidate(
                    &request_service, (const uint8_t *)invalid_request, (uint32_t)written,
                    &request) != FIRMWARE_STATUS_OK);

    puts("update_request_service_test: PASS");
    return 0;
}
