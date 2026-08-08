/**
 * @file manifest_service.c
 * @brief 严格校验 raw-bin-v1 Manifest，并生成完整性摘要。
 *
 * 本实现只接受冻结的生产 Manifest schema：JSON 文档先由受限解析器转换为 token，
 * 再逐层校验字段集合、类型、常量值和边界。解析成功后仅把完整结果一次性输出，
 * 防止调用方在错误路径中使用部分填充的 Manifest。
 */
#include "services/capability/manifest_service.h"

#include <stddef.h>
#include <string.h>

/** 当前支持的生产 Manifest schema 版本。 */
#define MANIFEST_FORMAT_VERSION 1U
/** Application 原始镜像允许写入 APP 区域的最大字节数。 */
#define APP_MAXIMUM_IMAGE_SIZE  1048576UL
/** GUI 原始资源允许写入 GUI 区域的最大字节数。 */
#define GUI_MAXIMUM_IMAGE_SIZE  8388608UL

/**
 * 查找对象中的一个成员值 token。
 *
 * 该局部包装器统一本文件中的 schema 查找调用，使验证辅助函数保持相同的错误语义。
 *
 * @param document 已完成语法解析的 JSON 文档。
 * @param object 目标对象 token 索引。
 * @param key 要求存在的 ASCII 成员名。
 * @param value 成功时接收成员值 token 索引。
 * @return 底层查找状态；找不到成员或对象非法时返回错误。
 */
static firmware_status_t FindMember(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    return JsonDocument_FindMember(document, object, key, value);
}

/**
 * 严格验证对象的成员集合。
 *
 * 同时检查直接成员数和每个预期成员是否存在；JSON 解析器已拒绝重复键，因此该组合
 * 可拒绝缺失字段、未知字段和重复字段，保证签名/哈希所依赖的 schema 不发生漂移。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 待校验的对象 token 索引。
 * @param names 允许且必须出现的成员名数组。
 * @param name_count 成员名数组元素数。
 * @return 成功时返回 FIRMWARE_STATUS_OK；对象类型或成员集合不匹配时返回错误。
 */
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

/**
 * 获取指定对象成员，并要求其值为 JSON 字符串。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param token 成功时接收字符串值 token 索引。
 * @return 成功时返回 FIRMWARE_STATUS_OK；成员不存在或类型不是字符串时返回错误。
 */
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

/**
 * 要求对象成员为与给定字面量完全一致的字符串。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param expected 期望的固定 ASCII 字符串。
 * @return 成功时返回 FIRMWARE_STATUS_OK；类型或内容不匹配时返回错误。
 */
static firmware_status_t RequireConstantString(const json_document_t *document, uint32_t object,
                                               const char *key, const char *expected)
{
    uint32_t token;

    return (FirmwareStatus_IsOk(RequireString(document, object, key, &token)) &&
            JsonDocument_StringEquals(document, token, expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 获取指定对象成员，并将其规范十进制值转换为 uint32_t。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param value 成功时接收转换后的无符号整数。
 * @return 成功时返回 FIRMWARE_STATUS_OK；成员缺失、类型错误或数值溢出时返回错误。
 */
static firmware_status_t RequireU32(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    uint32_t token;
    firmware_status_t status = FindMember(document, object, key, &token);

    return FirmwareStatus_IsOk(status) ? JsonDocument_GetU32(document, token, value)
                                       : FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 要求对象成员为给定的固定 uint32_t 值。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param expected 期望的固定数值。
 * @return 成功时返回 FIRMWARE_STATUS_OK；成员值不匹配时返回错误。
 */
static firmware_status_t RequireConstantU32(const json_document_t *document, uint32_t object,
                                            const char *key, uint32_t expected)
{
    uint32_t value;

    return (FirmwareStatus_IsOk(RequireU32(document, object, key, &value)) && (value == expected))
               ? FIRMWARE_STATUS_OK
               : FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 检查字符串 token 是否满足长度及字符白名单。
 *
 * 用于 package_id；字母与数字始终允许，additional 指定的分隔符按调用场景附加允许。
 * 解析器已限制为未转义 ASCII，因此这里可直接逐字节检查。
 *
 * @param document 已解析的 JSON 文档。
 * @param token_index 字符串 token 索引。
 * @param minimum 允许的最小字符串长度。
 * @param maximum 允许的最大字符串长度。
 * @param additional 额外允许字符构成的以零结束字符串。
 * @return 符合模式时返回非零，否则返回零。
 */
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

/**
 * 将严格的 "major.minor.patch" 字符串解析为发布版本。
 *
 * 每一段必须为无前导零的十进制 uint16_t，最多五位；这既限制输入规模，也避免同一
 * 版本存在多种文本表示，从而保持 Manifest 格式稳定。
 *
 * @param document 已解析的 JSON 文档。
 * @param token_index 版本字符串 token 索引。
 * @param version 成功时接收三段版本号。
 * @return 成功时返回 FIRMWARE_STATUS_OK；格式非法或数值越界时返回错误。
 */
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

/**
 * 将一个小写十六进制 ASCII 字符转换为半字节值。
 *
 * @param value 待转换的 ASCII 字节。
 * @return 0 至 15 表示有效值；-1 表示不是允许的小写十六进制字符。
 */
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

/**
 * 读取对象中的 64 位小写十六进制 SHA-256 字符串。
 *
 * 输出缓冲区只在所有字符均合法后由逐字节转换写入；若长度或任一字符不符合冻结
 * 格式，则拒绝整个 Manifest。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key SHA-256 字段名。
 * @param output 成功时接收 32 字节摘要。
 * @return 成功时返回 FIRMWARE_STATUS_OK；字段格式错误时返回错误。
 */
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

/**
 * 解析并校验 release 对象。
 *
 * @param document 已解析的 JSON 文档。
 * @param root 根对象 token 索引。
 * @param manifest 成功时接收发布版本与构建号。
 * @return 成功时返回 FIRMWARE_STATUS_OK；release schema 或数值范围不合法时返回错误。
 */
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

/**
 * 解析并校验目标硬件约束。
 *
 * 生产包仅适用于固定产品与硬件组合；最低 Bootloader 版本解析为数值三元组，供
 * 后续版本策略服务进行比较。
 *
 * @param document 已解析的 JSON 文档。
 * @param root 根对象 token 索引。
 * @param manifest 成功时接收最低 Bootloader 版本。
 * @return 成功时返回 FIRMWARE_STATUS_OK；目标信息不匹配时返回错误。
 */
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

/**
 * 解析一个固定名称的 APP 或 GUI 组件对象。
 *
 * 组件文件名和格式均为生产契约常量；大小必须落在对应存储区域内，并要求 SHA-256
 * 使用严格的小写十六进制表示。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 组件对象 token 索引。
 * @param file 该组件允许的唯一文件名。
 * @param maximum_size 对应目标分区可接受的最大字节数。
 * @param component 成功时接收已校验的组件信息。
 * @return 成功时返回 FIRMWARE_STATUS_OK；schema、文件名、大小或摘要错误时返回错误。
 */
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

/**
 * 解析 components 对象中的 APP 和 GUI 固定组件。
 *
 * @param document 已解析的 JSON 文档。
 * @param root 根对象 token 索引。
 * @param manifest 成功时接收两个组件的元数据。
 * @return 成功时返回 FIRMWARE_STATUS_OK；组件集合不完整或任一组件非法时返回错误。
 */
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

/**
 * 校验根对象及其格式版本和 package_id。
 *
 * 根对象成员必须与 V1 schema 完全一致；package_id 在写入固定输出数组之前先经过
 * 长度和字符白名单检查。
 *
 * @param document 已解析的 JSON 文档。
 * @param manifest 成功时接收 package_id。
 * @return 成功时返回 FIRMWARE_STATUS_OK；根 schema 或 package_id 非法时返回错误。
 */
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

/**
 * 使用注入的哈希端口计算一段连续字节的 SHA-256 摘要。
 *
 * reset、update、finish 严格按顺序执行；前一步失败时不再调用后续回调，以避免在
 * 底层哈希上下文处于错误状态时继续操作。
 *
 * @param hash 已初始化且回调完整的哈希端口。
 * @param data 待摘要的连续字节。
 * @param size 待摘要字节数。
 * @param digest 成功时接收 32 字节 SHA-256 摘要。
 * @return 哈希端口返回的最终状态。
 */
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

/**
 * 校验哈希依赖并初始化 Manifest 服务。
 *
 * @param service 服务实例，必须由 Composition 静态创建且尚未初始化。
 * @param dependencies 包含哈希端口的依赖集合。
 * @return 成功时返回 FIRMWARE_STATUS_OK；重复初始化或依赖不完整时返回错误。
 */
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

/**
 * 解析、严格校验并摘要化一份完整生产 Manifest。
 *
 * 处理顺序固定为：语法解析、根对象、发布信息、目标信息、组件信息、原始文件摘要和
 * package_id 摘要。所有阶段通过后才将局部 parsed 副本写回 manifest，错误时输出
 * 参数保持调用前内容。
 *
 * @param service 已初始化的 Manifest 服务。
 * @param data Manifest 原始 UTF-8/ASCII 字节，必须是完整单一 JSON 对象。
 * @param size data 的字节数，不得超过静态文档上限。
 * @param manifest 成功时接收已校验的结构化 Manifest。
 * @return 成功时返回 FIRMWARE_STATUS_OK；语法、schema、哈希或范围检查失败时返回错误。
 */
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
    /* 按冻结 schema 由外到内校验，任何一步失败都阻止后续字段或哈希处理。 */
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
        /* package_id 摘要只保留前 128 位，供 Active Record 绑定包身份。 */
        memcpy(parsed.package_id_hash128, package_digest, MANIFEST_PACKAGE_HASH_SIZE);
        /* 仅在全部验证和两次摘要均成功后发布解析结果。 */
        *manifest = parsed;
    }
    return status;
}
