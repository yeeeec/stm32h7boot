#include <stdio.h>
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

static const char valid_manifest[] =
    "{"
    "\"format_version\":2,"
    "\"package_id\":\"hmi-app-gui-1.0.0+1\","
    "\"product_id\":\"HMI\","
    "\"hardware_id\":\"STM32H743-W25Q256\","
    "\"build_number\":1,"
    "\"created_utc\":\"2026-08-05T00:00:00Z\","
    "\"minimum_bootloader_version\":\"1.0.0\","
    "\"release_groups\":[{"
        "\"id\":\"app-gui\",\"version\":\"1.2.3\",\"atomic\":true,"
        "\"components\":[\"app\",\"gui\"]}],"
    "\"transaction\":{"
        "\"release_group_id\":\"app-gui\",\"strategy\":\"inactive-pair\","
        "\"commit_store\":\"eeprom\","
        "\"commit_condition\":\"all-components-crc-valid\"},"
    "\"crc32_parameters\":{"
        "\"name\":\"CRC-32/ISO-HDLC\",\"polynomial\":\"0x04C11DB7\","
        "\"initial_value\":\"0xFFFFFFFF\",\"reflect_input\":true,"
        "\"reflect_output\":true,\"xor_output\":\"0xFFFFFFFF\"},"
    "\"components\":[{"
        "\"id\":\"app\",\"file\":\"hmi.app.bin\","
        "\"format\":\"raw-xip-reloc-v2\",\"target\":\"inactive-app-slot\","
        "\"maximum_image_size_bytes\":1048576,\"file_size_bytes\":65,"
        "\"image_size_bytes\":65,\"source_crc32\":\"0123ABCD\","
        "\"target_crc32\":{\"app1\":\"12345678\",\"app2\":\"90ABCDEF\"},"
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"vector_offset\":0,\"entry_offset\":4,\"link_address\":2415919104,"
        "\"relocation\":{\"file\":\"hmi.app.reloc.bin\",\"count\":1,"
        "\"crc32\":\"00000000\",\"format\":\"hmi-reloc-v1\"}"
    "},{"
        "\"id\":\"gui\",\"file\":\"hmi.gui.bin\",\"format\":\"raw\","
        "\"target\":\"inactive-gui-slot\","
        "\"maximum_image_size_bytes\":8388608,\"file_size_bytes\":1,"
        "\"crc32\":\"89ABCDEF\","
        "\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\""
    "}],"
    "\"signature\":{"
        "\"algorithm\":\"ECDSA-P256-SHA256\",\"key_id\":\"test-key\","
        "\"canonicalization\":\"RFC8785\","
        "\"scope\":\"all-fields-except-signature.value\","
        "\"encoding\":\"base64\","
        "\"value\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\""
    "}}";

typedef struct
{
    hash_provider_t interface;
    uint32_t hash;
    uint32_t verify_count;
} fake_authenticator_t;

static firmware_status_t HashReset(void *context)
{
    fake_authenticator_t *fake = (fake_authenticator_t *)context;

    fake->hash = 2166136261UL;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashUpdate(void *context, const void *data, size_t size)
{
    fake_authenticator_t *fake = (fake_authenticator_t *)context;
    const uint8_t *bytes = (const uint8_t *)data;

    while (size-- != 0U)
    {
        fake->hash = (fake->hash ^ *bytes++) * 16777619UL;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t HashFinish(void *context, uint8_t digest[32])
{
    fake_authenticator_t *fake = (fake_authenticator_t *)context;
    uint32_t index;

    for (index = 0U; index < 32U; ++index)
    {
        digest[index] = (uint8_t)(fake->hash >> ((index & 3U) * 8U));
    }
    return FIRMWARE_STATUS_OK;
}

static void FakeAuthenticator_Init(fake_authenticator_t *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->interface.context = fake;
    fake->interface.reset = HashReset;
    fake->interface.update = HashUpdate;
    fake->interface.finish = HashFinish;
}

int main(void)
{
    fake_authenticator_t fake;
    manifest_service_t service = {0};
    manifest_service_dependencies_t dependencies;
    validated_manifest_t manifest;
    validated_manifest_t unchanged;
    char invalid_manifest[sizeof(valid_manifest)];
    char *field;

    FakeAuthenticator_Init(&fake);
    dependencies.hash = &fake.interface;
    ASSERT_TRUE(ManifestService_Init(&service, &dependencies) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)valid_manifest,
                    (uint32_t)strlen(valid_manifest), &manifest) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(strcmp(manifest.package_id, "hmi-app-gui-1.0.0+1") == 0);
    ASSERT_TRUE((manifest.release_version.major == 1U) &&
                (manifest.release_version.minor == 2U) &&
                (manifest.release_version.patch == 3U));
    ASSERT_TRUE(manifest.app.source_crc32 == 0x0123ABCDUL);
    ASSERT_TRUE(manifest.app.target_crc32_app2 == 0x90ABCDEFUL);
    ASSERT_TRUE(manifest.gui.crc32 == 0x89ABCDEFUL);
    ASSERT_TRUE(fake.verify_count == 0U);

    memcpy(invalid_manifest, valid_manifest, sizeof(valid_manifest));
    field = strstr(invalid_manifest, "\"product_id\":\"HMI\"");
    ASSERT_TRUE(field != NULL);
    memcpy(strstr(field, "HMI"), "BAD", 3U);
    memset(&unchanged, 0xA5, sizeof(unchanged));
    manifest = unchanged;
    ASSERT_TRUE(ManifestService_ParseAndValidate(
                    &service, (const uint8_t *)invalid_manifest,
                    (uint32_t)strlen(invalid_manifest), &manifest) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(&manifest, &unchanged, sizeof(manifest)) == 0);
    ASSERT_TRUE(fake.verify_count == 0U);

    puts("manifest_service_test: PASS");
    return 0;
}
