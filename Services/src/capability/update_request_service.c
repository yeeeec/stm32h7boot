/**
 * @file update_request_service.c
 * @brief 严格解析可信更新请求，并绑定其声明的 Manifest 原始字节。
 *
 * 更新请求是启动流程中触发安装的可信控制文件。本实现拒绝未知或重复字段，限制
 * package_id 格式，并要求请求摘要、已解析 Manifest 摘要和原始 Manifest 字节三者
 * 完全一致，避免请求被替换或指向另一份 Manifest。
 */
#include "services/capability/update_request_service.h"

#include <stddef.h>
#include <string.h>

/**
 * 严格验证对象的成员集合。
 *
 * 直接成员数必须与预期相同，且每个预期键都必须存在。结合 JSON 解析器的重复键
 * 拒绝逻辑，可同时拒绝缺失、未知和重复字段。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 待校验对象的 token 索引。
 * @param names 必须且仅可出现的成员名数组。
 * @param name_count 成员名数组元素数。
 * @return 成功时返回 FIRMWARE_STATUS_OK；schema 不匹配时返回错误。
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
        if (!FirmwareStatus_IsOk(JsonDocument_FindMember(document, object, names[index], &ignored)))
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    return FIRMWARE_STATUS_OK;
}

/**
 * 获取一个必需成员并转换为 uint32_t。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param value 成功时接收解析后的无符号整数。
 * @return 成功时返回 FIRMWARE_STATUS_OK；成员不存在、类型错误或数值越界时返回错误。
 */
static firmware_status_t RequireU32(const json_document_t *document, uint32_t object,
                                    const char *key, uint32_t *value)
{
    uint32_t token;

    return FirmwareStatus_IsOk(JsonDocument_FindMember(document, object, key, &token))
               ? JsonDocument_GetU32(document, token, value)
               : FIRMWARE_STATUS_INVALID_STATE;
}

/**
 * 获取一个必需成员并要求其为字符串 token。
 *
 * @param document 已解析的 JSON 文档。
 * @param object 父对象 token 索引。
 * @param key 必需成员名。
 * @param token 成功时接收字符串 token 索引。
 * @return 成功时返回 FIRMWARE_STATUS_OK；成员不存在或类型错误时返回错误。
 */
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

/**
 * 检查请求中的 package_id 是否符合生产格式。
 *
 * 允许 ASCII 字母、数字及 . _ + -，并限制为固定输出数组可容纳的长度；该检查在
 * CopyString 前执行，保证字符串复制既安全又与 Manifest 的 package_id 规则一致。
 *
 * @param document 已解析的 JSON 文档。
 * @param token package_id 字符串 token 索引。
 * @return 合法时返回非零，否则返回零。
 */
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

/**
 * 将一个小写十六进制 ASCII 字符转换为半字节值。
 *
 * @param value 待转换的 ASCII 字节。
 * @return 0 至 15 表示有效值；-1 表示无效字符。
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
 * 将请求中的 Manifest SHA-256 文本转换为 32 字节摘要。
 *
 * 只接受长度恰为 64 的小写十六进制字符串，避免不同大小写或编码形式影响可信
 * 请求格式和后续字节级比较。
 *
 * @param document 已解析的 JSON 文档。
 * @param token manifest_sha256 字符串 token 索引。
 * @param digest 成功时接收 32 字节 SHA-256 摘要。
 * @return 成功时返回 FIRMWARE_STATUS_OK；字段格式错误时返回错误。
 */
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

/**
 * 使用注入的哈希端口计算连续原始字节的 SHA-256 摘要。
 *
 * 回调严格遵循 reset、update、finish 顺序；任一步失败都立即返回，防止继续使用
 * 可能处于异常状态的哈希上下文。
 *
 * @param hash 已初始化且回调完整的哈希端口。
 * @param data 待计算摘要的原始字节。
 * @param size data 的字节数。
 * @param digest 成功时接收 SHA-256 摘要。
 * @return 哈希端口返回的最终状态。
 */
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

/**
 * 校验哈希依赖并初始化更新请求服务。
 *
 * @param service 服务实例，必须尚未初始化。
 * @param dependencies 包含完整 hash_provider_t 回调集的依赖对象。
 * @return 成功时返回 FIRMWARE_STATUS_OK；重复初始化或依赖不完整时返回错误。
 */
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

/**
 * 解析并严格校验一份完整的更新请求 JSON。
 *
 * 请求必须声明当前格式版本、requested=true、合法 package_id 和 Manifest SHA-256。
 * 所有检查均成功后，才将局部 parsed 结果写回 request，避免失败时产生部分可信状态。
 *
 * @param service 已初始化的更新请求服务。
 * @param data 更新请求的原始 JSON 字节。
 * @param size data 的字节数，不得超过请求文档上限。
 * @param request 成功时接收已校验的更新请求。
 * @return 成功时返回 FIRMWARE_STATUS_OK；语法、schema 或字段检查失败时返回错误。
 */
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
    /* 格式版本、触发标志、包身份和摘要必须全部通过后才发布解析结果。 */
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

/**
 * 验证更新请求与已读取、已解析 Manifest 之间的字节级绑定。
 *
 * 服务重新计算原始 Manifest 字节的 SHA-256，并同时与请求摘要、解析结果保留的摘要
 * 以及 package_id 比较。三者任一不一致均视为输入被替换或跨包混用。
 *
 * @param service 已初始化的更新请求服务。
 * @param request 已由 ParseAndValidate 成功解析的请求。
 * @param manifest_data 实际从介质读取的原始 Manifest 字节。
 * @param manifest_size manifest_data 的字节数，必须非零。
 * @param manifest 已由 Manifest 服务验证的结构化 Manifest。
 * @return 成功时返回 FIRMWARE_STATUS_OK；绑定不一致或哈希计算失败时返回错误。
 */
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
        /* 请求、原始字节和结构化 Manifest 必须指向同一个不可歧义的发布包。 */
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
