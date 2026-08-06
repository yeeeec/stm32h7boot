#include <stdio.h>
#include <string.h>

#include "crypto/micro_ecc_authenticator.h"
#include "crypto/sha256.h"
#include "services/capability/base64.h"
#include "services/capability/json_document.h"

#define ASSERT_TRUE(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            printf("assertion failed at line %d\n", __LINE__);                  \
            return 1;                                                            \
        }                                                                        \
    } while (0)

typedef struct
{
    char bytes[256];
    size_t size;
} output_buffer_t;

static int HexValue(char value)
{
    if ((value >= '0') && (value <= '9'))
    {
        return value - '0';
    }
    if ((value >= 'a') && (value <= 'f'))
    {
        return value - 'a' + 10;
    }
    return value - 'A' + 10;
}

static void DecodeHex(const char *hex, uint8_t *output, size_t output_size)
{
    size_t index;

    for (index = 0U; index < output_size; ++index)
    {
        output[index] = (uint8_t)((HexValue(hex[index * 2U]) << 4) |
                                  HexValue(hex[index * 2U + 1U]));
    }
}

static firmware_status_t BufferSink(
    void *context,
    const void *data,
    size_t size)
{
    output_buffer_t *output = (output_buffer_t *)context;

    if (size > (sizeof(output->bytes) - output->size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(&output->bytes[output->size], data, size);
    output->size += size;
    return FIRMWARE_STATUS_OK;
}

static int TestSha256(void)
{
    static const char empty_hex[] =
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855";
    static const char abc_hex[] =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    sha256_context_t context;
    uint8_t actual[32];
    uint8_t expected[32];

    DecodeHex(empty_hex, expected, sizeof(expected));
    ASSERT_TRUE(Sha256_Reset(&context) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(Sha256_Finish(&context, actual) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(actual, expected, sizeof(actual)) == 0);

    DecodeHex(abc_hex, expected, sizeof(expected));
    ASSERT_TRUE(Sha256_Reset(&context) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(Sha256_Update(&context, "a", 1U) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(Sha256_Update(&context, "bc", 2U) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(Sha256_Finish(&context, actual) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(memcmp(actual, expected, sizeof(actual)) == 0);
    return 0;
}

static int TestRfc6979P256(void)
{
    static const char public_key_hex[] =
        "60fed4ba255a9d31c961eb74c6356d68c"
        "049b8923b61fa6ce669622e60f29fb6"
        "7903fe1008b8bc99a41ae9e95628bc64"
        "f2f1b20c2d7e9f5177a3c294d4462299";
    static const char digest_hex[] =
        "af2bdbe1aa9b6ec1e2ade1d694f41fc7"
        "1a831d0268e9891562113d8a62add1bf";
    static const char signature_hex[] =
        "efd48b2aacb6a8fd1140dd9cd45e81d6"
        "9d2c877b56aaf991c34d0ea84eaf3716"
        "f7cb1c942d657c41d436c7a1b6e29f65"
        "f3e900dbb9aff4064dc4ab2f843acda8";
    micro_ecc_authenticator_t provider = {0};
    micro_ecc_authenticator_config_t config;
    const image_authenticator_t *authenticator;
    uint8_t public_key[64];
    uint8_t invalid_public_key[64] = {0};
    uint8_t digest[32];
    uint8_t signature[64];
    micro_ecc_authenticator_t invalid_provider = {0};

    DecodeHex(public_key_hex, public_key, sizeof(public_key));
    DecodeHex(digest_hex, digest, sizeof(digest));
    DecodeHex(signature_hex, signature, sizeof(signature));
    config.key_id = "rfc6979-test";
    config.public_key = public_key;
    ASSERT_TRUE(MicroEccAuthenticator_Init(&provider, &config) == FIRMWARE_STATUS_OK);
    config.public_key = invalid_public_key;
    ASSERT_TRUE(MicroEccAuthenticator_Init(&invalid_provider, &config) ==
                FIRMWARE_STATUS_INVALID_ARGUMENT);
    config.public_key = public_key;
    authenticator = MicroEccAuthenticator_Interface(&provider);
    ASSERT_TRUE(authenticator->verify_signature(
                    authenticator->context, "rfc6979-test", digest,
                    signature, sizeof(signature)) == FIRMWARE_STATUS_OK);
    signature[0] ^= 1U;
    ASSERT_TRUE(authenticator->verify_signature(
                    authenticator->context, "rfc6979-test", digest,
                    signature, sizeof(signature)) != FIRMWARE_STATUS_OK);
    signature[0] ^= 1U;
    ASSERT_TRUE(authenticator->verify_signature(
                    authenticator->context, "wrong-key", digest,
                    signature, sizeof(signature)) != FIRMWARE_STATUS_OK);
    return 0;
}

static int TestCanonicalJson(void)
{
    static const char input[] =
        "{\"signature\":{\"value\":\"x\",\"key_id\":\"k\"},"
        "\"z\":[3,{\"b\":2,\"a\":1}]}";
    static const char expected[] =
        "{\"signature\":{\"key_id\":\"k\"},"
        "\"z\":[3,{\"a\":1,\"b\":2}]}";
    static const char duplicate[] = "{\"a\":1,\"a\":2}";
    static const char escaped[] = "{\"a\":\"\\u0061\"}";
    static const char floating[] = "{\"a\":1.0}";
    json_document_t document;
    json_token_t tokens[32];
    output_buffer_t output = {{0}, 0U};
    uint32_t signature;

    ASSERT_TRUE(JsonDocument_Parse(
                    &document, (const uint8_t *)input,
                    (uint32_t)strlen(input), tokens, 32U) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(JsonDocument_FindMember(
                    &document, 0U, "signature", &signature) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(JsonDocument_Canonicalize(
                    &document, signature, "value", BufferSink,
                    &output) == FIRMWARE_STATUS_OK);
    ASSERT_TRUE(output.size == strlen(expected));
    ASSERT_TRUE(memcmp(output.bytes, expected, output.size) == 0);

    ASSERT_TRUE(JsonDocument_Parse(
                    &document, (const uint8_t *)duplicate,
                    (uint32_t)strlen(duplicate), tokens, 32U) != FIRMWARE_STATUS_OK);
    ASSERT_TRUE(JsonDocument_Parse(
                    &document, (const uint8_t *)escaped,
                    (uint32_t)strlen(escaped), tokens, 32U) ==
                FIRMWARE_STATUS_NOT_SUPPORTED);
    ASSERT_TRUE(JsonDocument_Parse(
                    &document, (const uint8_t *)floating,
                    (uint32_t)strlen(floating), tokens, 32U) != FIRMWARE_STATUS_OK);
    return 0;
}

static int TestBase64(void)
{
    uint8_t decoded[4];
    size_t size;

    ASSERT_TRUE(Base64_DecodeStrict(
                    "AQIDBA==", 8U, decoded, sizeof(decoded), &size) ==
                FIRMWARE_STATUS_OK);
    ASSERT_TRUE((size == 4U) && (decoded[0] == 1U) && (decoded[1] == 2U) &&
                (decoded[2] == 3U) && (decoded[3] == 4U));
    ASSERT_TRUE(Base64_DecodeStrict(
                    "AR==", 4U, decoded, sizeof(decoded), &size) !=
                FIRMWARE_STATUS_OK);
    ASSERT_TRUE(Base64_DecodeStrict(
                    "AQI=\n", 5U, decoded, sizeof(decoded), &size) !=
                FIRMWARE_STATUS_OK);
    return 0;
}

int main(void)
{
    ASSERT_TRUE(TestSha256() == 0);
    ASSERT_TRUE(TestRfc6979P256() == 0);
    ASSERT_TRUE(TestCanonicalJson() == 0);
    ASSERT_TRUE(TestBase64() == 0);
    puts("crypto_json_test: PASS");
    return 0;
}
