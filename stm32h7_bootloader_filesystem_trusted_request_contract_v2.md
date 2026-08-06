# STM32H7 Bootloader 文件系统可信升级请求契约

> 版本：V2  
> 适用对象：Bootloader、正式 Application、当前人工测试流程、Host 打包/生产工具。  
> 当前介质：SD 卡 + SDIO/SDMMC + FatFs。  
> 未来介质：eMMC 或其他文件系统实现，通过 Adapter 替换。  
> 本文冻结文件路径、可信请求格式、Manifest 内容绑定、组件完整性校验、提交顺序和接口边界。

## 1. 核心安全模型

升级信任链固定为：

```text
发布工具使用私钥签名 Manifest
-> Application 使用受信公钥验证 Manifest 签名
-> Application 对最终 manifest.json 计算 SHA-256
-> Application 创建包含 manifest_sha256 的可信升级请求
-> Bootloader 校验 Manifest SHA-256
-> Bootloader 依据该 Manifest 校验 APP/GUI SHA-256
-> Bootloader 安装、目标 CRC 校验并提交 Active Record
```

职责结论：

- 请求文件既负责触发，也承担对一个确定 Manifest 的可信升级授权；
- Bootloader 不重复执行 Manifest ECDSA 验签；
- Bootloader 不持有 Manifest 验签公钥；
- Bootloader 必须校验 Manifest、APP 和 GUI 的 SHA-256；
- Manifest 的签名字段可以保留，但 Bootloader 不解释其安全语义；
- 请求的可信性来自正式 Application 的验签流程以及 Request Store 的写入保护，不来自普通 JSON 格式本身。

重要限制：

> 普通可移除 SD 卡上的 FAT 文件能够被人工修改，因此不天然具备可信认证能力。当前人工流程把操作者显式视为可信授权源，只适用于开发、调试或受控生产。量产环境若允许不受信主体修改文件系统，必须提供受保护分区、访问控制或其他可信 Request Store。

## 2. 固定文件布局

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

| 逻辑对象 | 绝对路径 | Bootloader 权限 |
|---|---|---|
| 可信升级请求 | `/boot_update_request.json` | 读取；提交成功后条件删除 |
| Manifest | `/firmware/manifest.json` | 只读 |
| APPX | `/firmware/hmi.app.bin` | 只读 |
| GUI | `/firmware/hmi.gui.bin` | 只读 |

禁止：

- 从请求文件读取任意目录或文件名；
- 接受绝对路径、`..`、路径分隔符或驱动器前缀；
- 由 Manifest 或请求扩大目标分区或指定物理地址；
- Bootloader 修改三个发布文件；
- 通过 SD 卡插入状态直接触发升级。

## 3. Trusted Update Request V2

### 3.1 JSON Schema

```json
{
  "format_version": 2,
  "requested": true,
  "package_id": "hmi-release-1.2.3-20260806",
  "manifest_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

### 3.2 字段规则

| 字段 | 类型 | 规则 |
|---|---|---|
| `format_version` | unsigned integer | 必须为 `2` |
| `requested` | boolean | 必须为 `true` |
| `package_id` | string | UTF-8，长度 `1..63`，仅允许 `[A-Za-z0-9._-]` |
| `manifest_sha256` | string | 完整 Manifest 文件 SHA-256，64 位小写十六进制 |

### 3.3 文件规则

- 最大文件长度：512 字节；
- 编码：UTF-8；
- 不允许 UTF-8 BOM；
- 允许 JSON 空白，不允许注释；
- 拒绝重复 Key、未知字段、缺失字段、`null` 和类型错误；
- 文件不存在与 I/O 错误必须返回不同状态；
- 请求不允许表达目标槽、跳过 Hash、允许降级或任意路径；
- 请求内容必须在创建后保持不变，直到 Bootloader 消费或清除。

### 3.4 授权语义

一个合法且来自受信 Request Store 的请求表示：

```text
创建者已经验证该 Manifest 的发布真实性
AND
授权 Bootloader 安装 manifest_sha256 标识的确切 Manifest
```

Bootloader 不能仅因为请求格式合法就默认其可信。Composition 绑定的 Request Store 必须满足本契约的受信来源要求。

## 4. Manifest 内容绑定

Bootloader 必须按原始文件字节计算：

```text
actual_manifest_sha256 = SHA256(/firmware/manifest.json)
```

只有满足以下条件，Manifest 内容才可用于后续安装：

```text
actual_manifest_sha256 == request.manifest_sha256
```

随后严格解析 Manifest，并要求：

```text
manifest.package_id == request.package_id
```

该顺序不能反转。未通过 Manifest SHA 绑定前，不得把 Manifest 中的版本、Hash、长度、目标 CRC 或重定位信息视为受信安装元数据。

## 5. 组件完整性校验

Manifest SHA 绑定通过后，Bootloader 计算：

```text
SHA256(/firmware/hmi.app.bin) == manifest.components.app.sha256
SHA256(/firmware/hmi.gui.bin) == manifest.components.gui.sha256
```

在擦除目标 APP 前还必须完成：

- Manifest Schema 严格解析；
- 产品和硬件兼容检查；
- 版本与防回滚检查；
- APPX Header CRC、长度和格式检查；
- Relocation Table CRC、排序、范围和类型检查；
- 所有地址与长度 checked arithmetic。

Bootloader 不执行：

- Manifest ECDSA 验签；
- 公钥选择、Key ID 或轮换；
- 签名 Canonicalization；
- `signature.value` 的 Base64 解码。

## 6. 当前人工测试流程

人工准备 SD 卡时按以下顺序执行：

1. 删除旧 `/boot_update_request.json`；
2. 写入三个 `/firmware` 文件；
3. 确认发布包来源可信；
4. 对完整 `manifest.json` 原始字节计算 SHA-256；
5. 从 Manifest 读取 `package_id`；
6. 生成 V2 请求；
7. 刷新主机写缓存；
8. 最后写入 `/boot_update_request.json`；
9. 安全弹出 SD 卡；
10. 插入设备并复位。

建议 Host 工具：

```text
create_update_request.py \
    --manifest /firmware/manifest.json \
    --output /boot_update_request.json
```

该工具必须从实际 Manifest 计算 SHA，禁止由操作者手工抄写 Hash。

人工模式的信任含义是“操作者已授权此 Manifest”，而不是 Bootloader 已完成密码学验签。

## 7. 正式 Application 提交流程

正式 Application 必须：

```text
下载/解密发布文件
-> 写临时文件并同步
-> 对最终 Manifest 执行 ECDSA P-256 验签
-> 校验产品、硬件和 Application 侧发布策略
-> 计算最终 Manifest SHA-256
-> 原子替换三个正式发布文件
-> 生成 V2 请求
-> 写 boot_update_request.tmp 并同步
-> 原子重命名为 boot_update_request.json
-> 系统复位
```

约束：

- 验签对象必须与最终正式 `manifest.json` 字节完全一致；
- 请求必须最后出现；
- 请求出现后不得再修改任何发布文件；
- Manifest 验签失败时不得创建请求；
- Application 不写 EEPROM Active Record；
- 文件系统无法提供可靠提交语义时，必须扩展本契约，不能暴露半写请求。

## 8. Bootloader 读取与安装顺序

```text
挂载文件系统
-> 加载并严格解析可信请求
-> 计算 Manifest SHA-256
-> 比较 request.manifest_sha256
-> 解析 Manifest
-> 比较 request.package_id
-> 检查产品/硬件/版本/防回滚
-> 校验 APP SHA-256
-> 校验 APPX/Relocation
-> 校验 GUI SHA-256
-> 检查是否已安装相同 package_id_hash128 + manifest_sha256
-> 安装非激活 APP/GUI 对
-> 校验目标 CRC
-> 提交 EEPROM Active Record
-> 读回验证
-> 条件删除请求
-> 系统复位
```

禁止：

- 调用 Manifest 签名验证器；
- Manifest SHA 不匹配时继续解析并安装；
- APP/GUI SHA 未完成前擦除目标 APP；
- Active Record 提交前删除请求；
- 请求删除失败时回退 Active Record；
- 已提交相同包时重复擦写。

## 9. Package Identity 与陈旧请求

EEPROM Active Record 保存：

```text
package_id_hash128 = SHA256(UTF-8 package_id)[0..15]
manifest_sha256     = request.manifest_sha256
```

相同包识别条件：

```text
Active Record 有效
AND active.package_id_hash128 == SHA256(request.package_id)[0..15]
AND active.manifest_sha256 == request.manifest_sha256
```

匹配时：

- 不重新擦除或写入；
- 重新加载请求；
- 确认 `package_id + manifest_sha256` 未被替换；
- 删除请求；
- 删除失败只产生告警；
- 使用已提交激活对。

## 10. 掉电矩阵

| 掉电位置 | Active Record | 请求文件 | 下次启动行为 |
|---|---|---|---|
| 发布文件写入期间 | 旧 | 不应存在 | 启动旧 APP；无有效槽时 Recovery |
| Application 验签前 | 旧 | 不应存在 | 不升级 |
| 请求创建期间 | 旧 | 可能损坏 | 严格解析失败，不安装 |
| Manifest/组件 SHA 期间 | 旧 | 存在 | 重新校验 |
| APP/GUI 擦写期间 | 旧 | 存在 | 从头重写非激活对 |
| Active Record 提交期间 | 旧或新有效副本 | 存在 | 通过 A/B、CRC、Commit Marker 选择 |
| Active Record 提交后、请求删除前 | 新 | 存在 | 识别已安装，只清请求 |
| 请求删除期间 | 新 | 存在或不存在 | 使用新激活对；必要时再次清理 |
| 请求删除后、系统复位前 | 新 | 不存在 | 使用新激活对 |

## 11. Interface 合同

### 11.1 `package_source_t`

用途：只读访问固定升级包文件。

```text
mount
unmount
is_media_present
open
close
get_size
read_at
```

### 11.2 `update_request_store_t`

用途：加载和条件删除可信请求。

```c
typedef struct
{
    void *context;

    firmware_status_t (*load)(
        void *context,
        uint8_t *buffer,
        size_t capacity,
        size_t *actual_size);

    firmware_status_t (*remove)(void *context);
    firmware_status_t (*sync)(void *context);
} update_request_store_t;
```

合同要求：

- `load` 只访问 `/boot_update_request.json`；
- `NOT_FOUND` 与 I/O 错误分离；
- Adapter 不解析 JSON；
- Service 在删除前重新确认 `package_id + manifest_sha256`；
- 正式实现必须保证不受信写入者不能创建、替换或回滚请求；
- 当前人工 SD 实现必须显式标记为开发信任覆盖，不能误称为量产安全实现。

### 11.3 当前与未来绑定

```text
当前开发：
package_source_t       <- FatFs SD Package Source Adapter
update_request_store_t <- FatFs SD Request Adapter（manual trust override）

正式产品：
package_source_t       <- eMMC/受控文件系统 Package Adapter
update_request_store_t <- 具备受信写入保证的 Request Store Adapter
```

Services 与 Application 不得依赖 SD、eMMC、FatFs 或 HAL 类型。

## 12. 稳定状态与错误

建议稳定状态：

```text
REQUEST_NOT_FOUND
REQUEST_VALID
REQUEST_INVALID
REQUEST_IO_ERROR
REQUEST_NOT_TRUSTED
MANIFEST_HASH_MISMATCH
REQUEST_PACKAGE_MISMATCH
REQUEST_ALREADY_INSTALLED
REQUEST_CLEARED
REQUEST_CLEAR_WARNING
```

Bootloader 不定义 `SIGNATURE_INVALID`，因为 Manifest 验签不属于 Bootloader 运行路径。

## 13. 验收条件

- [ ] 请求 Schema 使用 V2；
- [ ] 请求包含 `manifest_sha256`；
- [ ] Bootloader 先校验 Manifest SHA，再信任 Manifest 内容；
- [ ] Bootloader 不链接 Manifest 签名验证器或公钥；
- [ ] Request `package_id` 只与 SHA 绑定后的 Manifest 比较；
- [ ] APP/GUI SHA 在擦除前完成；
- [ ] 请求不能指定目标槽或路径；
- [ ] Active Record 提交前请求始终存在；
- [ ] Active Record 提交后请求删除失败不回退；
- [ ] 相同包陈旧请求不会触发重复擦写；
- [ ] 当前人工流程明确标记为开发信任覆盖；
- [ ] 正式 Request Store 的受信写入保证有可验证设计；
- [ ] 替换 eMMC Adapter 时 Services 无需修改。
