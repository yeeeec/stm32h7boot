#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "services/capability/manifest_service.h"

#define ASSERT_TRUE(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            printf("assertion failed at line %d\n", __LINE__);                  \
            return 1;                                                            \
        }                                                                        \
    } while (0)

/* This is the byte sequence whose digest is bound by the request test. */
const char valid_manifest[] =
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

static int TestValidManifest(fake_hash_t *fake, manifest_service_t *service,
                             validated_manifest_t *manifest)
{
    manifest_service_dependencies_t dependencies = {.hash = &fake->interface};

    ASSERT_TRUE(ManifestService_Init(service, &dependencies) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    service, (const uint8_t *)valid_manifest,
                    (uint32_t)strlen(valid_manifest), manifest) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(strcmp(manifest->package_id, "hmi-app-gui-1.2.3+42") == 0);
    ASSERT_TRUE((manifest->release_version.major == 1U) &&
                (manifest->release_version.minor == 2U) &&
                (manifest->release_version.patch == 3U) &&
                (manifest->build_number == 42U));
    ASSERT_TRUE((manifest->app.size_bytes == 64U) && (manifest->gui.size_bytes == 128U));
    ASSERT_TRUE((manifest->app.sha256[0] == 0U) && (manifest->gui.sha256[0] == 0x11U));
    ASSERT_TRUE(strcmp(manifest->app.file, "hmi.app.bin") == 0);
    ASSERT_TRUE(strcmp(manifest->gui.file, "hmi.gui.bin") == 0);
    return 0;
}

static int TestComponentCombinations(fake_hash_t *fake, manifest_service_t *service)
{
    static const char app_json[] =
        "\"app\":{\"file\":\"hmi.app.bin\",\"format\":\"raw-bin-v1\","
        "\"size\":64,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    static const char gui_json[] =
        "\"gui\":{\"file\":\"hmi.gui.bin\",\"format\":\"raw-bin-v1\","
        "\"size\":128,\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\"}";
    static const char therapy_json[] =
        "\"therapy\":{\"file\":\"therapy.app.bin\",\"format\":\"raw-bin-v1\","
        "\"size\":256,\"sha256\":\"2222222222222222222222222222222222222222222222222222222222222222\"}";
    char components[2048];
    char document[4096];
    uint32_t mask;
    int written;

    for (mask = UPDATE_COMPONENT_APP; mask <= UPDATE_COMPONENT_ALL; ++mask)
    {
        size_t offset = 0U;
        int first = 1;

        components[offset++] = '{';
        if ((mask & UPDATE_COMPONENT_APP) != 0U)
        {
            written = snprintf(&components[offset], sizeof(components) - offset, "%s%s",
                               first ? "" : ",", app_json);
            ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(components) - offset));
            offset += (size_t)written;
            first = 0;
        }
        if ((mask & UPDATE_COMPONENT_GUI) != 0U)
        {
            written = snprintf(&components[offset], sizeof(components) - offset, "%s%s",
                               first ? "" : ",", gui_json);
            ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(components) - offset));
            offset += (size_t)written;
            first = 0;
        }
        if ((mask & UPDATE_COMPONENT_THERAPY) != 0U)
        {
            written = snprintf(&components[offset], sizeof(components) - offset, "%s%s",
                               first ? "" : ",", therapy_json);
            ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(components) - offset));
            offset += (size_t)written;
        }
        written = snprintf(&components[offset], sizeof(components) - offset, "}");
        ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(components) - offset));
        written = snprintf(document, sizeof(document),
                           "{\"format_version\":1,\"package_id\":\"combo-%lu\","
                           "\"release\":{\"major\":1,\"minor\":0,\"patch\":0,\"build\":1},"
                           "\"target\":{\"product\":\"HMI\",\"hardware\":\"STM32H743-W25Q256\","
                           "\"minimum_bootloader_version\":\"1.0.0\"},\"components\":%s}",
                           (unsigned long)mask, components);
        ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(document)));
        {
            validated_manifest_t parsed;
            ASSERT_TRUE(ManifestService_ParseAndValidate(
                            service, (const uint8_t *)document, (uint32_t)written,
                            &parsed) == FIRMWARE_STATUS_OK);
            ASSERT_TRUE(parsed.component_mask == mask);
        }
    }
    (void)fake;
    return 0;
}

static int TestTherapySizeLimit(fake_hash_t *fake, manifest_service_t *service)
{
    char document[2048];
    validated_manifest_t parsed;
    int written;

    written = snprintf(document, sizeof(document),
                       "{\"format_version\":1,\"package_id\":\"therapy-limit\","
                       "\"release\":{\"major\":1,\"minor\":0,\"patch\":0,\"build\":1},"
                       "\"target\":{\"product\":\"HMI\",\"hardware\":\"STM32H743-W25Q256\","
                       "\"minimum_bootloader_version\":\"1.0.0\"},\"components\":{"
                       "\"therapy\":{\"file\":\"therapy.app.bin\",\"format\":\"raw-bin-v1\","
                       "\"size\":%lu,\"sha256\":\"2222222222222222222222222222222222222222222222222222222222222222\"}}}",
                       (unsigned long)UPDATE_THERAPY_IMAGE_MAX_SIZE);
    ASSERT_TRUE((written > 0) && ((size_t)written < sizeof(document)));
    ASSERT_TRUE(ManifestService_ParseAndValidate(service, (const uint8_t *)document,
                                                 (uint32_t)written, &parsed) == FIRMWARE_STATUS_OK);
    /* Build the same document with one byte beyond the upper bound. */
    {
        char *size_field = strstr(document, "\"size\":524288");
        ASSERT_TRUE(size_field != NULL);
        size_field[strlen("\"size\":52428")] = '9';
        ASSERT_TRUE(ManifestService_ParseAndValidate(service, (const uint8_t *)document,
                                                     (uint32_t)strlen(document), &parsed) !=
                    FIRMWARE_STATUS_OK);
    }
    (void)fake;
    return 0;
}

int main(void)
{
    fake_hash_t fake;
    manifest_service_t service = {0};
    validated_manifest_t manifest;
    validated_manifest_t unchanged;
    char invalid_manifest[sizeof(valid_manifest)];
    char *field;

    FakeHash_Init(&fake);
    ASSERT_TRUE(TestValidManifest(&fake, &service, &manifest) == 0);
    ASSERT_TRUE(TestComponentCombinations(&fake, &service) == 0);
    ASSERT_TRUE(TestTherapySizeLimit(&fake, &service) == 0);

    /* V1 is a distinct contract and rejects unsupported legacy members. */
    memcpy(invalid_manifest, valid_manifest, sizeof(valid_manifest));
    field = strstr(invalid_manifest, "\"format_version\":1");
    ASSERT_TRUE(field != NULL);
    field[strlen("\"format_version\":")] = '2';
    unchanged = manifest;
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)invalid_manifest,
                    (uint32_t)strlen(invalid_manifest), &manifest) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&manifest, &unchanged, sizeof(manifest)) == 0);

    memcpy(invalid_manifest, valid_manifest, sizeof(valid_manifest));
    field = strstr(invalid_manifest, "\"format\":\"raw-bin-v1\"");
    ASSERT_TRUE(field != NULL);
    field[strlen("\"format\":\"raw-bin-v")] = '2';
    unchanged = manifest;
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)invalid_manifest,
                    (uint32_t)strlen(invalid_manifest), &manifest) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&manifest, &unchanged, sizeof(manifest)) == 0);

    memcpy(invalid_manifest, valid_manifest, sizeof(valid_manifest));
    field = strstr(invalid_manifest, "\"size\":64");
    ASSERT_TRUE(field != NULL);
    field[1] = 'x';
    unchanged = manifest;
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)invalid_manifest,
                    (uint32_t)strlen(invalid_manifest), &manifest) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&manifest, &unchanged, sizeof(manifest)) == 0);

    memcpy(invalid_manifest, valid_manifest, sizeof(valid_manifest));
    field = strstr(invalid_manifest, "\"sha256\":\"000");
    ASSERT_TRUE(field != NULL);
    field[strlen("\"sha256\":\"")] = 'A';
    unchanged = manifest;
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)invalid_manifest,
                    (uint32_t)strlen(invalid_manifest), &manifest) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&manifest, &unchanged, sizeof(manifest)) == 0);

    puts("manifest_service_test: PASS");
    return 0;
}
