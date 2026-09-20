/**
 * @file update_manifest.c
 * @brief 无堆内存的升级 manifest JSON 解析、规范化和摘要计算。
 *
 * 解析器只接受定义好的升级 schema，并在复制字符串/数字前执行边界检查；
 * 签名验证由独立的静态信任库模块完成。
 */

#include "update_internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "crypto/sha256.h"
#include "firmware/product_identity.h"
#include "platform/platform_memory_map.h"
#include "update_config.h"

/* 解析游标不拥有输入缓冲区，整个解析过程不申请堆内存。 */
typedef struct
{
    const uint8_t *data; /* 不拥有的 JSON 输入缓冲区。 */
    size_t length;       /* 输入缓冲区总长度。 */
    size_t position;     /* 当前解析偏移。 */
} JsonCursor;


/** 跳过 JSON 空白字符。 */
static void SkipSpace(JsonCursor *cursor)
{
    while (cursor->position < cursor->length)
    {
        uint8_t c = cursor->data[cursor->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        ++cursor->position;
    }
}

/** 匹配并消费一个期望字符。 */
static int Consume(JsonCursor *cursor, uint8_t expected)
{
    SkipSpace(cursor);
    if (cursor->position >= cursor->length || cursor->data[cursor->position] != expected)
        return 0;
    ++cursor->position;
    return 1;
}
static int HasTrailingObjectComma(const uint8_t *json, size_t length)
{
    size_t position;
    int in_string = 0;
    int escaped   = 0;

    for (position = 0U; position < length; ++position)
    {
        uint8_t c = json[position];
        if (in_string != 0)
        {
            if (escaped != 0)
                escaped = 0;
            else if (c == '\\')
                escaped = 1;
            else if (c == '"')
                in_string = 0;
            continue;
        }
        if (c == '"')
        {
            in_string = 1;
            continue;
        }
        if (c == ',')
        {
            size_t next = position + 1U;
            while (next < length && (json[next] == ' ' || json[next] == '\t' ||
                                     json[next] == '\r' || json[next] == '\n'))
                ++next;
            if (next < length && json[next] == '}')
                return 1;
        }
    }
    return 0;
}

/** 读取 JSON 字符串并复制到固定容量缓冲区。 */
static int CopyString(JsonCursor *cursor, char *destination, size_t capacity)
{
    size_t out = 0U;
    SkipSpace(cursor);
    if (cursor->position >= cursor->length || cursor->data[cursor->position++] != '"' ||
        capacity == 0U)
        return 0;
    while (cursor->position < cursor->length)
    {
        uint8_t c = cursor->data[cursor->position++];
        if (c == '"')
        {
            destination[out] = '\0';
            return 1;
        }
        if (c < 0x20U)
            return 0;
        if (c == '\\')
        {
            if (cursor->position >= cursor->length)
                return 0;
            c = cursor->data[cursor->position++];
            /* 明确拒绝 UTF-16 转义；manifest 使用 UTF-8 且标识符为
             * ASCII，避免签名数据出现多种写法。 */
            if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' &&
                c != 't')
                return 0;
            switch (c)
            {
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    break;
            }
        }
        if (out + 1U >= capacity)
            return 0;
        destination[out++] = (char) c;
    }
    return 0;
}

/** 读取对象键名。 */
static int ReadKey(JsonCursor *cursor, char *key, size_t capacity)
{
    return CopyString(cursor, key, capacity);
}

/** 检查字符串是否为规范 ASCII 且已正确终止。 */
static int IsCanonicalAsciiValue(const char *value, size_t capacity)
{
    size_t i;
    if (value == NULL || value[0] == '\0')
        return 0;
    for (i = 0U; i < capacity && value[i] != '\0'; ++i)
    {
        const unsigned char c = (unsigned char) value[i];
        if (c < 0x20U || c > 0x7EU || c == '"' || c == '\\')
            return 0;
    }
    return i < capacity;
}

/** 检查 SHA-256 十六进制文本格式。 */
static int IsSha256Hex(const char *value)
{
    size_t i;
    if (value == NULL || strlen(value) != UPDATE_SHA256_HEX_LENGTH)
        return 0;
    for (i = 0U; i < UPDATE_SHA256_HEX_LENGTH; ++i)
    {
        const unsigned char c = (unsigned char) value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return 1;
}

/** 读取无符号十进制整数并检查溢出。 */
static int ReadUint(JsonCursor *cursor, uint32_t *value)
{
    uint64_t number = 0U;
    size_t digits   = 0U;
    SkipSpace(cursor);
    if (cursor->position < cursor->length && cursor->data[cursor->position] == '0')
    {
        ++cursor->position;
        digits = 1U;
        if (cursor->position < cursor->length && cursor->data[cursor->position] >= '0' &&
            cursor->data[cursor->position] <= '9')
            return 0; /* 不允许前导零。 */
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

/** 读取 release 版本对象。 */
static int ReadVersion(JsonCursor *cursor, update_version_t *version)
{
    char key[24];
    uint32_t seen = 0U;
    if (!Consume(cursor, '{') || version == NULL)
        return 0;
    for (;;)
    {
        uint32_t value;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!ReadKey(cursor, key, sizeof(key)) || !Consume(cursor, ':') ||
            !ReadUint(cursor, &value))
            return 0;
        if (strcmp(key, "major") == 0 && (seen & 1U) == 0U)
        {
            version->major = value;
            seen |= 1U;
        }
        else if (strcmp(key, "minor") == 0 && (seen & 2U) == 0U)
        {
            version->minor = value;
            seen |= 2U;
        }
        else if (strcmp(key, "patch") == 0 && (seen & 4U) == 0U)
        {
            version->patch = value;
            seen |= 4U;
        }
        else if (strcmp(key, "build") == 0 && (seen & 8U) == 0U)
        {
            version->build = value;
            seen |= 8U;
        }
        else
            return 0;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!Consume(cursor, ','))
            return 0;
    }
    return (seen & 15U) == 15U;
}

/** 读取数字或字符串形式的 bootloader 最低版本。 */
static int ReadVersionValue(JsonCursor *cursor, update_version_t *version, uint8_t *is_string)
{
    char text[48];
    uint32_t values[3] = {0U, 0U, 0U};
    size_t i;
    SkipSpace(cursor);
    if (cursor->position < cursor->length && cursor->data[cursor->position] == '"')
    {
        if (!CopyString(cursor, text, sizeof(text)))
            return 0;
        {
            size_t position = 0U;
            for (i = 0U; i < 3U; ++i)
            {
                uint32_t value = 0U;
                size_t digits  = 0U;
                while (text[position] >= '0' && text[position] <= '9')
                {
                    uint32_t digit = (uint32_t) (text[position] - '0');
                    if (value > (UINT32_MAX - digit) / 10U)
                        return 0;
                    value = value * 10U + digit;
                    ++position;
                    ++digits;
                }
                if (digits == 0U || (i < 2U ? text[position++] != '.' : text[position] != '\0'))
                    return 0;
                values[i] = value;
            }
        }
        version->major = (uint32_t) values[0];
        version->minor = (uint32_t) values[1];
        version->patch = (uint32_t) values[2];
        version->build = 0U;
        if (is_string != NULL)
            *is_string = 1U;
        return 1;
    }
    if (is_string != NULL)
        *is_string = 0U;
    return ReadVersion(cursor, version);
}

/** 读取并校验 target 对象。 */
static int ReadTarget(JsonCursor *cursor, update_manifest_t *manifest)
{
    char key[40];
    uint32_t seen = 0U;
    if (!Consume(cursor, '{'))
        return 0;
    for (;;)
    {
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!ReadKey(cursor, key, sizeof(key)) || !Consume(cursor, ':'))
            return 0;
        if (strcmp(key, "product") == 0 && (seen & 1U) == 0U)
        {
            if (!CopyString(cursor, manifest->product, sizeof(manifest->product)))
                return 0;
            seen |= 1U;
        }
        else if (strcmp(key, "hardware") == 0 && (seen & 2U) == 0U)
        {
            if (!CopyString(cursor, manifest->hardware, sizeof(manifest->hardware)))
                return 0;
            seen |= 2U;
        }
        else if (strcmp(key, "minimum_bootloader_version") == 0 && (seen & 4U) == 0U)
        {
            if (!ReadVersionValue(cursor, &manifest->minimum_bootloader_version,
                                  &manifest->minimum_bootloader_version_is_string))
                return 0;
            seen |= 4U;
        }
        else
            return 0;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!Consume(cursor, ','))
            return 0;
    }
    return seen == 7U;
}

/** 将组件名称映射为传输位掩码。 */
static uint32_t ComponentMask(const char *name)
{
    const update_component_descriptor_t *descriptor = UpdateComponent_Find(name);
    return descriptor != NULL ? descriptor->mask_bit : 0U;
}

/** 读取单个组件定义。 */
static int ReadComponent(JsonCursor *cursor, update_manifest_component_t *component)
{
    char key[24];
    uint32_t seen = 0U;
    if (!Consume(cursor, '{'))
        return 0;
    for (;;)
    {
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!ReadKey(cursor, key, sizeof(key)) || !Consume(cursor, ':'))
            return 0;
        if (strcmp(key, "file") == 0 && (seen & 1U) == 0U)
        {
            if (!CopyString(cursor, component->file, sizeof(component->file)) ||
                !UpdatePackage_IsValidComponentFileName(component->file))
                return 0;
            seen |= 1U;
        }
        else if (strcmp(key, "format") == 0 && (seen & 2U) == 0U)
        {
            if (!CopyString(cursor, component->format, sizeof(component->format)))
                return 0;
            seen |= 2U;
        }
        else if (strcmp(key, "size") == 0 && (seen & 4U) == 0U)
        {
            if (!ReadUint(cursor, &component->size))
                return 0;
            seen |= 4U;
        }
        else if (strcmp(key, "sha256") == 0 && (seen & 8U) == 0U)
        {
            if (!CopyString(cursor, component->sha256, sizeof(component->sha256)))
                return 0;
            seen |= 8U;
        }
        else if (strcmp(key, "crc32") == 0 && (seen & 16U) == 0U)
        {
            if (!ReadUint(cursor, &component->crc32))
                return 0;
            component->has_crc32 = 1U;
            seen |= 16U;
        }
        else
            return 0;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!Consume(cursor, ','))
            return 0;
    }
    return (seen & 15U) == 15U && strlen(component->sha256) == UPDATE_SHA256_HEX_LENGTH;
}

/** 读取 components 对象及其所有组件。 */
static int ReadComponents(JsonCursor *cursor, update_manifest_t *manifest)
{
    char key[UPDATE_COMPONENT_NAME_MAX];
    uint32_t seenMask = 0U;
    if (!Consume(cursor, '{'))
        return 0;
    for (;;)
    {
        update_manifest_component_t *component;
        uint32_t mask;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (manifest->component_count >= UPDATE_MANIFEST_MAX_COMPONENTS ||
            !ReadKey(cursor, key, sizeof(key)) || !Consume(cursor, ':'))
            return 0;
        mask = ComponentMask(key);
        if (mask == 0U || (seenMask & mask) != 0U)
            return 0;
        component = &manifest->components[manifest->component_count];
        (void) memset(component, 0, sizeof(*component));
        (void) memcpy(component->name, key, strlen(key) + 1U);
        {
            const update_component_descriptor_t *descriptor = UpdateComponent_Find(key);
            if (descriptor == NULL)
                return 0;
            component->target = descriptor->target;
            component->mask_bit = descriptor->mask_bit;
            component->installation_order = descriptor->installation_order;
        }
        if (!ReadComponent(cursor, component))
            return 0;
        seenMask |= mask;
        ++manifest->component_count;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!Consume(cursor, ','))
            return 0;
    }
    manifest->component_mask = seenMask;
    return manifest->component_count != 0U;
}

/** 读取 signing 对象及签名元数据。 */
static int ReadSigning(JsonCursor *cursor, update_manifest_t *manifest)
{
    char key[32];
    uint32_t seen = 0U;
    if (!Consume(cursor, '{'))
        return 0;
    for (;;)
    {
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!ReadKey(cursor, key, sizeof(key)) || !Consume(cursor, ':'))
            return 0;
        if (strcmp(key, "format_version") == 0 && (seen & 1U) == 0U)
        {
            if (!ReadUint(cursor, &manifest->format_version) ||
                manifest->format_version != UPDATE_SUPPORTED_MANIFEST_VERSION)
                return 0;
            seen |= 1U;
        }
        else if (strcmp(key, "algorithm") == 0 && (seen & 2U) == 0U)
        {
            if (!CopyString(cursor, manifest->algorithm, sizeof(manifest->algorithm)))
                return 0;
            seen |= 2U;
        }
        else if (strcmp(key, "key_id") == 0 && (seen & 4U) == 0U)
        {
            if (!CopyString(cursor, manifest->key_id, sizeof(manifest->key_id)))
                return 0;
            seen |= 4U;
        }
        else if (strcmp(key, "created_at") == 0 && (seen & 8U) == 0U)
        {
            if (!CopyString(cursor, manifest->created_at, sizeof(manifest->created_at)))
                return 0;
            seen |= 8U;
        }
        else if (strcmp(key, "payload_format") == 0 && (seen & 16U) == 0U)
        {
            if (!CopyString(cursor, manifest->payload_format, sizeof(manifest->payload_format)))
                return 0;
            seen |= 16U;
        }
        else if (strcmp(key, "signature_encoding") == 0 && (seen & 32U) == 0U)
        {
            if (!CopyString(cursor, manifest->signature_encoding,
                            sizeof(manifest->signature_encoding)))
                return 0;
            seen |= 32U;
        }
        else if (strcmp(key, "signature") == 0 && (seen & 64U) == 0U)
        {
            if (!CopyString(cursor, manifest->signature, sizeof(manifest->signature)))
                return 0;
            seen |= 64U;
        }
        else
            return 0;
        SkipSpace(cursor);
        if (Consume(cursor, '}'))
            break;
        if (!Consume(cursor, ','))
            return 0;
    }
    if (seen != 127U)
        return 0;
    manifest->has_signing = 1U;
    return 1;
}

/** 检查升级包 ID 是否满足安全字符集和长度限制。 */
int UpdatePackage_IsValidId(const char *value)
{
    size_t i;
    if (value == NULL || value[0] == '\0')
        return 0;
    for (i = 0U; i < UPDATE_PACKAGE_ID_MAX && value[i] != '\0'; ++i)
    {
        unsigned char c = (unsigned char) value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.' || c == '+'))
            return 0;
    }
    return i != 0U && i < UPDATE_PACKAGE_ID_MAX;
}

/** 检查组件文件名，拒绝路径穿越和特殊字符。 */
int UpdatePackage_IsValidComponentFileName(const char *value)
{
    size_t i;
    if (value == NULL || value[0] == '\0')
        return 0;
    for (i = 0U; i < UPDATE_COMPONENT_FILE_MAX && value[i] != '\0'; ++i)
    {
        unsigned char c = (unsigned char) value[i];
        if (c == '/' || c == '\\' || (c == '.' && (i == 0U || value[i - 1U] == '.')) ||
            c == '?' || c == '<' || c == '>' || c == '|' || c == ';')
            return 0;
        if (c < 0x21U || c > 0x7EU || c == ':' || c == '"' || c == '*')
            return 0;
    }
    return i != 0U && i < UPDATE_COMPONENT_FILE_MAX;
}

firmware_status_t UpdateManifest_ValidateTarget(const update_manifest_t *manifest)
{
    size_t i;
    uint32_t expected = 0U;
    if (manifest == NULL || strcmp(manifest->product, FIRMWARE_PRODUCT_NAME) != 0 ||
        strcmp(manifest->hardware, FIRMWARE_HARDWARE_NAME) != 0 ||
        manifest->format_version != UPDATE_SUPPORTED_MANIFEST_VERSION ||
        manifest->component_count == 0U || manifest->component_count > UPDATE_MANIFEST_MAX_COMPONENTS)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (i = 0U; i < manifest->component_count; ++i)
    {
        const update_manifest_component_t *component = &manifest->components[i];
        const update_component_descriptor_t *descriptor = UpdateComponent_Find(component->name);
        if (descriptor == NULL || component->mask_bit != descriptor->mask_bit ||
            component->target != descriptor->target || component->size == 0U ||
            strcmp(component->format, descriptor->format) != 0 ||
            !IsSha256Hex(component->sha256) || strcmp(component->file, UPDATE_MANIFEST_FILE) == 0)
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        if ((expected & descriptor->mask_bit) != 0U)
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        expected |= descriptor->mask_bit;
        for (size_t previous = 0U; previous < i; ++previous)
            if (strcmp(component->file, manifest->components[previous].file) == 0)
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (expected != manifest->component_mask || UpdateComponent_DeriveMask(manifest) != expected)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return UpdateComponent_ValidateRanges(manifest);
}

/** 解析完整 manifest 并执行 schema、范围和规范性校验。 */
firmware_status_t UpdateManifest_Parse(const uint8_t *json, size_t length,
                                       update_manifest_t *manifest)
{
    JsonCursor cursor = {json, length, 0U};
    char key[32];
    uint32_t seen = 0U;
    if (json == NULL || manifest == NULL || length == 0U || length > UPDATE_MANIFEST_MAX_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (HasTrailingObjectComma(json, length))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(manifest, 0, sizeof(*manifest));
    if (!Consume(&cursor, '{'))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (;;)
    {
        SkipSpace(&cursor);
        if (Consume(&cursor, '}'))
            break;
        if (!ReadKey(&cursor, key, sizeof(key)) || !Consume(&cursor, ':'))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        if (strcmp(key, "format_version") == 0 && (seen & 1U) == 0U)
        {
            if (!ReadUint(&cursor, &manifest->format_version) ||
                manifest->format_version != UPDATE_SUPPORTED_MANIFEST_VERSION)
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 1U;
        }
        else if (strcmp(key, "package_id") == 0 && (seen & 2U) == 0U)
        {
            if (!CopyString(&cursor, manifest->package_id, sizeof(manifest->package_id)) ||
                !UpdatePackage_IsValidId(manifest->package_id))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 2U;
        }
        else if (strcmp(key, "release") == 0 && (seen & 4U) == 0U)
        {
            if (!ReadVersion(&cursor, &manifest->release))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 4U;
        }
        else if (strcmp(key, "target") == 0 && (seen & 8U) == 0U)
        {
            if (!ReadTarget(&cursor, manifest))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 8U;
        }
        else if (strcmp(key, "components") == 0 && (seen & 16U) == 0U)
        {
            if (!ReadComponents(&cursor, manifest))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 16U;
        }
        else if (strcmp(key, "signing") == 0 && (seen & 32U) == 0U)
        {
            if (!ReadSigning(&cursor, manifest))
                return FIRMWARE_STATUS_INVALID_ARGUMENT;
            seen |= 32U;
        }
        else
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        SkipSpace(&cursor);
        if (Consume(&cursor, '}'))
            break;
        if (!Consume(&cursor, ','))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    SkipSpace(&cursor);
    if (cursor.position != cursor.length || (seen & 63U) != 63U ||
        manifest->component_count == 0U || manifest->has_signing == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!IsCanonicalAsciiValue(manifest->product, sizeof(manifest->product)) ||
        !IsCanonicalAsciiValue(manifest->hardware, sizeof(manifest->hardware)) ||
        (manifest->has_signing != 0U &&
         (!IsCanonicalAsciiValue(manifest->algorithm, sizeof(manifest->algorithm)) ||
          !IsCanonicalAsciiValue(manifest->key_id, sizeof(manifest->key_id)) ||
          !IsCanonicalAsciiValue(manifest->payload_format, sizeof(manifest->payload_format)) ||
          !IsCanonicalAsciiValue(manifest->signature_encoding,
                                 sizeof(manifest->signature_encoding)) ||
          !IsCanonicalAsciiValue(manifest->signature, sizeof(manifest->signature)) ||
          !IsCanonicalAsciiValue(manifest->created_at, sizeof(manifest->created_at)))))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0U; i < manifest->component_count; ++i)
    {
        if (!IsCanonicalAsciiValue(manifest->components[i].format,
                                   sizeof(manifest->components[i].format)) ||
            !IsSha256Hex(manifest->components[i].sha256))
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return FIRMWARE_STATUS_OK;
}

/** 按 major/minor/patch/build 字段比较两个版本。 */
int UpdateVersion_Compare(const update_version_t *left, const update_version_t *right)
{
    uint32_t l[4], r[4];
    size_t i;
    if (left == NULL || right == NULL)
        return 0;
    l[0] = left->major;
    l[1] = left->minor;
    l[2] = left->patch;
    l[3] = left->build;
    r[0] = right->major;
    r[1] = right->minor;
    r[2] = right->patch;
    r[3] = right->build;
    for (i = 0U; i < 4U; ++i)
        if (l[i] != r[i])
            return l[i] < r[i] ? -1 : 1;
    return 0;
}

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    crypto_sha256_context_t *hash;
} CanonicalWriter;

static firmware_status_t CanonicalWrite(CanonicalWriter *writer, const uint8_t *data, size_t size)
{
    if (writer->hash != NULL)
        return Crypto_Sha256Update(writer->hash, data, size) == 0 ? FIRMWARE_STATUS_OK
                                                                  : FIRMWARE_STATUS_IO_ERROR;
    if (writer->length > writer->capacity || size > writer->capacity - writer->length)
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    (void) memcpy(writer->buffer + writer->length, data, size);
    writer->length += size;
    return FIRMWARE_STATUS_OK;
}

/** 向规范化输出器追加一段常量文本。 */
static firmware_status_t Append(CanonicalWriter *writer, const char *text)
{
    if (writer == NULL || text == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return CanonicalWrite(writer, (const uint8_t *) text, strlen(text));
}

/** 按固定键顺序追加版本对象。 */
static firmware_status_t AppendVersion(CanonicalWriter *writer, const update_version_t *version,
                                       int include_build)
{
    char text[64];
    int count;
    if (include_build != 0)
    {
        count = snprintf(text, sizeof(text),
                         "{\"build\":%lu,\"major\":%lu,\"minor\":%lu,\"patch\":%lu}",
                         (unsigned long) version->build, (unsigned long) version->major,
                         (unsigned long) version->minor, (unsigned long) version->patch);
    }
    else
    {
        count = snprintf(text, sizeof(text), "{\"major\":%lu,\"minor\":%lu,\"patch\":%lu}",
                         (unsigned long) version->major, (unsigned long) version->minor,
                         (unsigned long) version->patch);
    }
    if (count <= 0 || (size_t) count >= sizeof(text))
        return FIRMWARE_STATUS_OVERFLOW;
    return CanonicalWrite(writer, (const uint8_t *) text, (size_t) count);
}

/** 追加兼容数字/字符串表示的最低 bootloader 版本。 */
static firmware_status_t AppendMinimumBootloaderVersion(CanonicalWriter *writer,
                                                        const update_manifest_t *manifest)
{
    char text[48];
    int count;
    if (manifest->minimum_bootloader_version_is_string == 0U)
        return AppendVersion(writer, &manifest->minimum_bootloader_version, 0);
    count = snprintf(text, sizeof(text), "\"%lu.%lu.%lu\"",
                     (unsigned long) manifest->minimum_bootloader_version.major,
                     (unsigned long) manifest->minimum_bootloader_version.minor,
                     (unsigned long) manifest->minimum_bootloader_version.patch);
    if (count <= 0 || (size_t) count >= sizeof(text))
        return FIRMWARE_STATUS_OVERFLOW;
    return CanonicalWrite(writer, (const uint8_t *) text, (size_t) count);
}

/** 按协议规定的键顺序写出 canonical manifest（不含签名字段值）。 */
static firmware_status_t WriteCanonical(const update_manifest_t *manifest, CanonicalWriter *writer)
{
    /* 组件输出顺序属于 canonical 格式，保证相同 manifest 始终得到相同摘要。 */
    size_t order[UPDATE_MANIFEST_MAX_COMPONENTS];
    size_t component_count = 0U;
    size_t i;
    size_t j;
    firmware_status_t status;

#define APPEND_TEXT(text)                                                                          \
    do                                                                                             \
    {                                                                                              \
        status = Append(writer, (text));                                                           \
        if (FirmwareStatus_IsError(status))                                                        \
            return status;                                                                         \
    } while (0)

    if (manifest == NULL || writer == NULL || !UpdatePackage_IsValidId(manifest->package_id))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    for (i = 0U; i < manifest->component_count; ++i)
        order[i] = i;
    for (i = 0U; i < manifest->component_count; ++i)
        for (j = i + 1U; j < manifest->component_count; ++j)
            if (strcmp(manifest->components[order[j]].name,
                       manifest->components[order[i]].name) < 0)
            {
                size_t temporary = order[i];
                order[i] = order[j];
                order[j] = temporary;
            }

    APPEND_TEXT("{\"components\":{");
    for (i = 0U; i < manifest->component_count; ++i)
    {
        const update_manifest_component_t *component = &manifest->components[order[i]];
        char field[UPDATE_COMPONENT_FILE_MAX + UPDATE_COMPONENT_FORMAT_MAX +
                   UPDATE_SHA256_HEX_LENGTH + 128U];
        int count;

        if (component_count++ != 0U)
            APPEND_TEXT(",");

        if (component->has_crc32 != 0U)
        {
            count = snprintf(field, sizeof(field),
                             "\"%s\":{\"crc32\":%lu,\"file\":\"%s\",\"format\":\"%s\","
                             "\"sha256\":\"%s\",\"size\":%lu}",
                             component->name, (unsigned long) component->crc32, component->file,
                             component->format, component->sha256, (unsigned long) component->size);
        }
        else
        {
            count = snprintf(field, sizeof(field),
                             "\"%s\":{\"file\":\"%s\",\"format\":\"%s\",\"sha256\":\"%s\","
                             "\"size\":%lu}",
                             component->name, component->file, component->format, component->sha256,
                             (unsigned long) component->size);
        }
        if (count <= 0 || (size_t) count >= sizeof(field))
            return FIRMWARE_STATUS_OVERFLOW;
        status = CanonicalWrite(writer, (const uint8_t *) field, (size_t) count);
        if (FirmwareStatus_IsError(status))
            return status;
    }
    if (component_count == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    APPEND_TEXT("},\"format_version\":1,\"package_id\":\"");
    APPEND_TEXT(manifest->package_id);
    APPEND_TEXT("\",\"release\":");
    status = AppendVersion(writer, &manifest->release, 1);
    if (FirmwareStatus_IsError(status))
        return status;
    if (manifest->has_signing != 0U)
    {
        APPEND_TEXT(",\"signing\":{\"algorithm\":\"");
        APPEND_TEXT(manifest->algorithm);
        APPEND_TEXT("\",\"created_at\":\"");
        APPEND_TEXT(manifest->created_at);
        APPEND_TEXT("\",\"format_version\":1,\"key_id\":\"");
        APPEND_TEXT(manifest->key_id);
        APPEND_TEXT("\",\"payload_format\":\"");
        APPEND_TEXT(manifest->payload_format);
        APPEND_TEXT("\",\"signature_encoding\":\"");
        APPEND_TEXT(manifest->signature_encoding);
        APPEND_TEXT("\"}");
    }
    APPEND_TEXT(",\"target\":{\"hardware\":\"");
    APPEND_TEXT(manifest->hardware);
    APPEND_TEXT("\",\"minimum_bootloader_version\":");
    status = AppendMinimumBootloaderVersion(writer, manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    APPEND_TEXT(",\"product\":\"");
    APPEND_TEXT(manifest->product);
    APPEND_TEXT("\"}}");
#undef APPEND_TEXT
    return FIRMWARE_STATUS_OK;
}

/** 将 manifest 规范化写入文本缓冲区。 */
firmware_status_t UpdateManifest_Canonicalize(const update_manifest_t *manifest, char *buffer,
                                              size_t capacity, size_t *length)
{
    CanonicalWriter writer = {buffer, capacity, 0U, NULL};
    firmware_status_t status;

    if (buffer == NULL || length == NULL || capacity == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = WriteCanonical(manifest, &writer);
    if (FirmwareStatus_IsOk(status))
        *length = writer.length;
    return status;
}

/** 对 canonical manifest 计算 SHA-256 摘要。 */
firmware_status_t UpdateManifest_Digest(const update_manifest_t *manifest, uint8_t digest[32])
{
    crypto_sha256_context_t hash;
    CanonicalWriter writer = {NULL, 0U, 0U, &hash};
    firmware_status_t status;

    if (manifest == NULL || digest == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = Crypto_Sha256Init(&hash);
    if (FirmwareStatus_IsOk(status))
        status = WriteCanonical(manifest, &writer);
    if (FirmwareStatus_IsOk(status))
        status = Crypto_Sha256Finish(&hash, digest);
    else
        Crypto_Sha256Abort(&hash);
    return status;
}

/** 将 manifest 摘要转换为小写十六进制字符串。 */
firmware_status_t UpdateManifest_Hash(const update_manifest_t *manifest,
                                      char output[UPDATE_SHA256_HEX_LENGTH + 1U])
{
    /* 固定字符表避免 locale 影响摘要文本。 */
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE];
    firmware_status_t status;
    size_t i;

    if (output == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = UpdateManifest_Digest(manifest, digest);
    if (FirmwareStatus_IsError(status))
        return status;
    for (i = 0U; i < sizeof(digest); ++i)
    {
        output[i * 2U]      = hex[digest[i] >> 4];
        output[i * 2U + 1U] = hex[digest[i] & 0x0FU];
    }
    output[UPDATE_SHA256_HEX_LENGTH] = '\0';
    return FIRMWARE_STATUS_OK;
}
