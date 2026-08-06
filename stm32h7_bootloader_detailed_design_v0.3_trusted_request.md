# STM32H7 Bootloader 详细设计

> 状态：设计草案 v0.3  
> 依据：`stm32h7_bootloader_business_rules_v0.8_trusted_request.md`  
> 范围：APP/GUI 双槽升级、W25Q256 XIP、AT24C128AN Active Record、SD 卡 FatFs 升级文件系统、文件型升级请求。  
> 当前验证阶段由人工准备文件并置位请求；正式阶段由 Application 完成相同动作。

## 1. 设计结论

本设计固定以下实现方向：

1. Bootloader 位于 STM32H743 内部 Flash；
2. APP1/APP2 直接从 W25Q256 QSPI Memory-Mapped 区域执行；
3. APP1+GUI1 和 APP2+GUI2 构成两个原子发布槽；
4. 升级始终覆盖非激活槽，EEPROM Active Record 提交前激活槽不变；
5. 当前升级文件系统位于 SD 卡，通过 SDIO/SDMMC 和 FatFs 接入；
6. 未来 eMMC 通过替换 Adapter 接入，不修改上层接口和升级事务；
7. 文件系统根目录使用 `/boot_update_request.json` 表达升级请求；
8. 升级包由 `/firmware/manifest.json`、`/firmware/hmi.app.bin`、`/firmware/hmi.gui.bin` 组成；
9. APP/GUI 共享一个发布版本；未来 Audio 使用独立发布组和独立版本；
10. 升级包只有一个 APPX，APPX 通过安装时重定位适配两个 XIP 基址；
11. AT24C128AN 只保存 Active Record 双副本，不再保存 Update Request；
12. Application 在创建可信请求前执行 Manifest ECDSA P-256 验签；Bootloader 不重复验签；
13. 长流程由 `update_service_process()` 增量执行，不使用动态内存；
14. QSPI、FatFs、SDIO/SDMMC、EEPROM、CRC、SHA-256、复位和跳转均通过 Interface/Adapter 隔离；
15. 升级成功后由 Application 顶层状态机进入 `RESET`，统一执行系统复位。

## 2. 固定地址与容量

### 2.1 W25Q256 物理布局

W25Q256 容量为 32 MiB，Flash 偏移范围为 `0x0000000..0x1FFFFFF`，QSPI Memory-Mapped 基址为 `0x90000000`。

| 区域 | Flash 偏移 | CPU 地址 | 大小 | 用途 |
|---|---:|---:|---:|---|
| APP1 | `0x000000` | `0x90000000` | 1 MiB | 发布槽 1 的 XIP APP |
| APP2 | `0x100000` | `0x90100000` | 1 MiB | 发布槽 2 的 XIP APP |
| GUI1 | `0x200000` | `0x90200000` | 8 MiB | 发布槽 1 的 GUI 资源 |
| GUI2 | `0xA00000` | `0x90A00000` | 8 MiB | 发布槽 2 的 GUI 资源 |
| RESERVED | `0x1200000` | `0x91200000` | 14 MiB | 未来 Audio 或其他数据，当前禁止访问 |

所有区域按 4 KiB 擦除边界对齐。Bootloader 使用固定表查找区域，不接受 Manifest 或请求文件提供的物理地址。

### 2.2 QSPI 参数

W25Q256 需要 25 个地址位，因此 STM32 HAL `QSPI_InitTypeDef.FlashSize` 必须设置为 `24`。当前 CubeMX 值 `25` 必须在启用 XIP 前修正。

首版 Memory-Mapped 读取使用保守模式：

- 指令：Fast Read `0x0B`；
- Instruction：1 line；
- Address：4 bytes、1 line；
- Data：1 line；
- Dummy cycles：8；
- DDR：关闭；
- Timeout counter：关闭；
- SIOO：每次发送指令。

后续切换 Quad I/O Read 必须单独验证 QE 位、Dummy cycle、Cache 和复位恢复。

### 2.3 AT24C128AN 布局

AT24C128AN 按 16 KiB 地址空间设计，地址范围 `0x0000..0x3FFF`。首版按 64-byte 页设计，实际页大小和 I2C 地址仍需用量产器件数据手册及板级原理图确认。

| EEPROM 地址 | 大小 | 用途 |
|---|---:|---|
| `0x0000` | 256 B | Active Record A |
| `0x0100` | 256 B | Active Record B |
| `0x0200` | 640 B | Boot metadata 保留；原 Update Request 区域并入保留区 |
| `0x0480` | 896 B | 未来 Audio/control 保留 |
| `0x0800` | 14336 B | 未分配，禁止隐式使用 |

本版不在 EEPROM 中保存升级请求，也不持久化每个 Flash 块的升级进度。

## 3. 发布槽模型

```text
PAIR_1 = APP1 + GUI1
PAIR_2 = APP2 + GUI2
```

槽位选择只允许：

```text
active_pair == PAIR_1 -> target_pair = PAIR_2
active_pair == PAIR_2 -> target_pair = PAIR_1
```

不允许：

- APP1 + GUI2；
- APP2 + GUI1；
- 单独提交 APP；
- 单独提交 GUI；
- Manifest 或请求文件指定任意 Flash 地址；
- EEPROM Active Record 提交前跳转目标槽。

`release_version` 属于 APP/GUI 发布组，不分别保存 `app_version` 和 `gui_version`。

## 4. 文件系统升级契约

### 4.1 固定目录

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

路径在产品只读配置中冻结：

```c
#define BOOT_UPDATE_REQUEST_PATH  "/boot_update_request.json"
#define BOOT_PACKAGE_DIRECTORY    "/firmware"
#define BOOT_MANIFEST_PATH        "/firmware/manifest.json"
#define BOOT_APP_PATH             "/firmware/hmi.app.bin"
#define BOOT_GUI_PATH             "/firmware/hmi.gui.bin"
```

Service 不拼接来自请求或 Manifest 的任意目录。Manifest 中的组件文件名必须是基础文件名，且最终解析结果只能匹配冻结文件名。

### 4.2 可信请求文件 V2

请求文件最大长度固定为 512 字节，UTF-8 编码，不允许 BOM。

唯一合法 Schema：

```json
{
  "format_version": 2,
  "requested": true,
  "package_id": "hmi-release-1.2.3-20260806",
  "manifest_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

解析器必须：

- 使用 JSON tokenizer/parser，不使用字符串查找；
- 拒绝重复 Key；
- 拒绝未知字段；
- 拒绝缺失、`null` 或类型错误字段；
- `format_version` 必须是十进制无符号整数 `2`；
- `requested` 必须是 JSON 布尔值 `true`；
- `package_id` 长度为 `1..63` 字节，只允许 `[A-Za-z0-9._-]`；
- `manifest_sha256` 必须是 64 位小写十六进制字符串；
- 不允许请求文件携带路径、文件名、目标槽、版本覆盖或降级标志。

可信语义：

- 正式 Application 只有在 Manifest ECDSA 验签成功后才能创建该文件；
- 请求中的 `manifest_sha256` 对完整 `manifest.json` 进行内容绑定；
- Bootloader 将一个从受信 Request Store 读取到的合法请求视为升级授权；
- Bootloader 不解析或验证 Manifest 的 `signature` 字段；
- Bootloader 通过计算 Manifest SHA-256 确认自己读取的是被授权 Manifest；
- 之后 APP/GUI SHA-256 均以该已绑定 Manifest 中的值为依据。

安全前提：普通可移除 FAT 文件没有天然认证能力。量产实现必须保证请求文件只能由受信 Application 或生产工具创建，且在 Bootloader 消费前不能被不受信主体修改。当前人工方式属于开发信任覆盖。

### 4.3 当前人工置位

当前测试阶段允许人工直接创建可信请求。操作者在该模式下被视为授权源，必须保证：

1. 三个发布文件已经完整写入 `/firmware`；
2. 已确认发布包来源可信；
3. 计算完整 `/firmware/manifest.json` 文件的 SHA-256；
4. 最后创建 `/boot_update_request.json`，写入正确的 `package_id` 和 `manifest_sha256`；
5. 文件已经关闭并从主机侧安全弹出/同步；
6. 完成后复位设备。

人工模式不提供量产级防伪能力，但 Bootloader 仍会执行 Manifest/APP/GUI SHA、格式、版本、边界、重定位和目标 CRC 检查。

### 4.4 正式 Application 置位

正式 Application 必须采用“验签成功后、请求最后提交”规则：

```text
写临时发布文件
-> 关闭并同步
-> 计算完整 Manifest SHA-256
-> 验证 Manifest ECDSA P-256 签名
-> 校验产品、硬件、版本和下载策略
-> 原子替换为三个正式发布文件
-> 生成含 package_id + manifest_sha256 的请求
-> 写 /boot_update_request.tmp
-> 关闭并同步
-> 原子重命名为 /boot_update_request.json
-> 系统复位
```

禁止：

- 在 Manifest 验签完成前创建正式请求；
- 验签的 Manifest 与最终写入文件不是同一字节序列；
- 创建请求后继续修改 Manifest 或组件文件；
- Application 写 EEPROM Active Record。

文件系统不支持可靠原子重命名时，Application 侧必须另行设计提交协议。

### 4.5 Bootloader 清除请求

Bootloader 只在新 Active Record 提交并读回成功后调用请求清除接口。

清除规则：

1. 再次加载请求；
2. 确认 `package_id` 和 `manifest_sha256` 与本次已提交包一致；
3. 删除 `/boot_update_request.json`；
4. 同步目录元数据（底层能力支持时）；
5. 删除失败不回退 Active Record；
6. 下次启动通过 `package_id_hash128 + manifest_sha256` 识别陈旧请求，只执行清理，不重复擦写。

## 5. Manifest、可信授权与完整性契约

### 5.1 Manifest 解析规则

Manifest 最大长度固定为 16 KiB，使用静态缓冲区。解析器必须：

- 使用 JSON tokenizer/parser；
- 拒绝重复 Key；
- 拒绝未知的顶层和组件字段；
- 拒绝缺失、`null` 或类型错误的生产字段；
- 数值必须为十进制无符号整数且可表示为 `uint32_t`；
- CRC 使用 8 位大写十六进制字符串；
- SHA-256 使用 64 位小写十六进制字符串；
- 文件名最大 31 字节，仅允许 `[A-Za-z0-9._-]`；
- 版本固定为 `major.minor.patch`，每段范围为 `0..65535`；
- `components` 必须恰好包含一个 `app` 和一个 `gui`；
- `release_groups` 必须包含原子 `app-gui` 组；
- `target` 只能是 `inactive-app-slot` 或 `inactive-gui-slot`；
- `maximum_image_size_bytes` 必须等于 Bootloader 固定配置；
- Manifest 中 APP/GUI 文件名必须分别等于 `hmi.app.bin`、`hmi.gui.bin`。

组件校验字段含义：

| 字段 | 覆盖范围 |
|---|---|
| APP `sha256` | 完整 `hmi.app.bin`，包括 APPX Header、Canonical Image 和 Relocation Table |
| APP `source_crc32` | Canonical APP Image，不包括 APPX Header 和 Relocation Table |
| APP `target_crc32.app1/app2` | 重定位后实际写入目标 APP 槽的数据 |
| GUI `sha256` | 完整 `hmi.gui.bin` |
| GUI `crc32` | 完整 `hmi.gui.bin` |

### 5.2 Application 验签与 Bootloader 信任交接

发布包仍使用 ECDSA P-256/SHA-256 签名，但职责固定为：

```text
发布工具：使用私钥生成 Manifest 签名
Application：使用受信公钥验证 Manifest 签名
Application：验签成功后创建绑定 manifest_sha256 的可信请求
Bootloader：不重复验签，只验证请求绑定和文件 SHA 完整性
```

Bootloader 不需要：

- micro-ecc；
- Manifest 验签公钥；
- Key ID 选择或公钥轮换逻辑；
- JSON Canonicalization 的签名输入重建；
- `signature_verifier_t`。

但 Bootloader 必须严格保证：

- 完整 Manifest SHA-256 与请求中的 `manifest_sha256` 一致；
- `request.package_id == manifest.package_id`；
- APP/GUI 文件 SHA-256 与已绑定 Manifest 一致；
- Manifest 格式、组件集合、长度、分区、版本、防回滚和重定位规则仍然有效。

### 5.3 擦除前校验顺序

```text
可信请求严格解析
-> 完整 Manifest SHA-256
-> request.manifest_sha256 == actual_manifest_sha256
-> Manifest 严格解析
-> request.package_id == manifest.package_id
-> 产品/硬件兼容检查
-> 版本/防回滚检查
-> APP 完整文件 SHA-256
-> APPX Header CRC 和格式
-> Relocation Table CRC、排序、范围和类型
-> GUI 完整文件 SHA-256
```

上述步骤全部通过后，才允许擦除目标 APP。

## 6. APPX 文件格式

`hmi.app.bin` 是固定小端格式的 `HMI_XIP_APP_V1` 容器：

```text
+----------------------------+
| 64-byte app container hdr  |
+----------------------------+
| canonical APP image bytes  |
+----------------------------+
| relocation entries         |
+----------------------------+
```

### 6.1 APPX 头

| 偏移 | 大小 | 字段 | 规则 |
|---:|---:|---|---|
| `0x00` | 4 | magic | ASCII `HAPX` |
| `0x04` | 2 | format_version | `1` |
| `0x06` | 2 | header_size | `64` |
| `0x08` | 4 | canonical_base | `0` |
| `0x0C` | 4 | image_size | `1..1048576` |
| `0x10` | 4 | vector_offset | 首版必须为 `0` |
| `0x14` | 4 | entry_offset | Reset Handler 相对 APP 基址偏移 |
| `0x18` | 4 | relocation_offset | 文件内偏移，4 字节对齐 |
| `0x1C` | 4 | relocation_count | 不超过产品上限 |
| `0x20` | 2 | relocation_entry_size | 首版为 `8` |
| `0x22` | 2 | flags | 首版必须为 `0` |
| `0x24` | 4 | image_crc32 | 规范基址 APP 数据 CRC |
| `0x28` | 4 | relocation_crc32 | 重定位表 CRC |
| `0x2C` | 4 | header_crc32 | 头部 CRC，本字段按 0 计算 |
| `0x30` | 16 | reserved | 必须全 0 |

Canonical APP image 紧跟 64-byte 头部。重定位表必须位于 APP 数据之后，且 `relocation_offset + count * entry_size` 必须等于文件长度。

### 6.2 精简重定位项

```c
typedef struct
{
    uint32_t target_offset;
    uint16_t type;
    uint16_t reserved;
} app_relocation_entry_v1_t;
```

首版只允许：

```text
type = 1: ABS32_ADD_XIP_BASE
final_word = canonical_word + target_xip_base
```

规则：

- `target_offset` 必须 4 字节对齐；
- `target_offset + 4 <= image_size`；
- 条目严格递增，不允许重复；
- `reserved` 必须为 0；
- 加法检查 `uint32_t` 溢出；
- 未知类型拒绝整个包；
- 初始 MSP 指向 SRAM，不得重定位；
- Reset Handler、IRQ Handler 和 APP 内部绝对地址必须有对应重定位项；
- MMIO、SRAM 和其他明确绝对地址不得增加 XIP 基址。

Host 工具从带 relocation 信息的 ELF 提取白名单条目。Bootloader 不解析 ELF。

### 6.3 流式重定位

APP 使用 4 KiB 对齐 I/O Buffer 流式处理：

1. 读取下一块 Canonical APP；
2. 读取落在当前块内的重定位项；
3. 在 RAM Buffer 中应用目标 XIP 基址；
4. 按 W25Q256 256-byte page 边界写入；
5. 完成后从 Flash 间接读取并计算最终 CRC；
6. 根据目标槽比较 `target_crc32.app1` 或 `target_crc32.app2`。

Host 工具必须模拟两个目标基址并生成两个目标 CRC，但升级包仍只有一份 APPX。

## 7. Active Record V1

每份 Active Record 固定 256 字节：

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| `0x00` | 4 | magic = `HBCR` |
| `0x04` | 2 | format_version = 1 |
| `0x06` | 2 | record_size = 256 |
| `0x08` | 4 | sequence |
| `0x0C` | 1 | state = `ACTIVE_VALID` |
| `0x0D` | 1 | active_pair (`1` 或 `2`) |
| `0x0E` | 2 | flags |
| `0x10` | 2 | release_major |
| `0x12` | 2 | release_minor |
| `0x14` | 2 | release_patch |
| `0x16` | 2 | reserved0 |
| `0x18` | 4 | build_number |
| `0x1C` | 4 | app_size |
| `0x20` | 4 | app_crc32 |
| `0x24` | 4 | gui_size |
| `0x28` | 4 | gui_crc32 |
| `0x2C` | 16 | package_id_hash128 |
| `0x3C` | 32 | manifest_sha256 |
| `0x5C` | 156 | reserved，写入 `0xFF` |
| `0xF8` | 4 | record_crc32，覆盖 `0x00..0xF7` |
| `0xFC` | 4 | commit_marker = `0x434F4D54` (`COMT`) |

记录不保存裸跳转地址。`active_pair` 通过固定表映射 APP/GUI 地址。

`package_id_hash128` 为 UTF-8 `package_id` 的 SHA-256 前 16 字节；`manifest_sha256` 为可信请求授权并由 Bootloader 实际校验通过的完整 Manifest 文件 SHA-256。

### 7.1 提交算法

1. 读取 A/B，检查 Magic、版本、范围、Record CRC 和 Commit Marker；
2. 使用模 32-bit 序列号比较选择最新有效副本；
3. 选择另一副本作为写入目标；
4. 将目标副本 Commit Marker 写为无效值并等待 EEPROM ready；
5. 按页写入 `0x00..0xFB`，每页 ACK polling；
6. 读回并验证全部内容及 Record CRC；
7. 最后单独写入 Commit Marker；
8. 再次读回 Commit Marker 和关键字段；
9. 只有步骤 8 成功，内存中的 Active Record 才切换到新副本。

任何一步掉电或失败，旧副本继续有效。若 A/B 序列号相同但内容不同，视为存储冲突并进入 Recovery。

## 8. Application 顶层状态机

| 状态 | 入口动作 | 周期动作 | 成功出口 | 失败出口 |
|---|---|---|---|---|
| `STARTUP` | 收集复位原因 | 检查 Composition 初始化 | `SELECT_MODE` | `FAULT` |
| `SELECT_MODE` | 启动模式选择能力 | 读取 Active Record 并有界检查请求 | `UPDATE`、`VALIDATE_ACTIVE` | `RECOVERY`、`FAULT` |
| `VALIDATE_ACTIVE` | 启动激活槽校验 | 增量 CRC APP/GUI | `LAUNCH` | `RECOVERY` |
| `UPDATE` | 启动 Update Service | 调用 `update_service_process()` | `RESET` | `RECOVERY` 或旧槽 `LAUNCH` |
| `RECOVERY` | 启动 Recovery Service | 检查备用槽或等待 SD 文件系统 | `VALIDATE_ACTIVE`、`UPDATE` | 保持等待或 `FAULT` |
| `RESET` | 启动 Reset Service | 执行受控系统复位 | 不返回 | `FAULT` |
| `LAUNCH` | 启动 XIP Launch Service | 不返回 | Application | `FAULT` |
| `FAULT` | 记录稳定错误 | 喂狗策略由产品配置决定 | 受控复位/恢复 | 保持 |

Application 只根据 Service 状态和稳定错误做转换，不包含 FatFs、SDIO/SDMMC、QSPI、CRC、EEPROM 或 HAL 技术步骤。

## 9. Update Service 状态机

### 9.1 外部状态

```text
IDLE
RUNNING
SUCCEEDED
FAILED
CANCELLED（仅擦除目标槽前允许）
```

### 9.2 内部阶段

```text
MOUNT_PACKAGE_FS
LOAD_UPDATE_REQUEST
VALIDATE_UPDATE_REQUEST
OPEN_MANIFEST
HASH_MANIFEST
VERIFY_MANIFEST_HASH
PARSE_MANIFEST
VALIDATE_MANIFEST
VERIFY_REQUEST_PACKAGE_ID
VERIFY_APP_SOURCE
VERIFY_GUI_SOURCE
CHECK_ALREADY_INSTALLED
SELECT_TARGET
PREPARE_APP
ERASE_APP
PROGRAM_APP
VERIFY_APP_TARGET
PREPARE_GUI
ERASE_GUI
PROGRAM_GUI
VERIFY_GUI_TARGET
COMMIT_ACTIVE_RECORD
CLEAR_UPDATE_REQUEST
COMPLETE
```

阶段约束：

- `MOUNT_PACKAGE_FS` 在正常模式下必须有界，不等待介质无限就绪；
- 可信请求、Manifest SHA、APPX Header、重定位表和两个源文件 SHA 全部通过后才允许擦除目标 APP；
- 每次 `process()` 最多执行一次短文件读取、一个擦除单元、一个数据块、一次异步轮询或一个状态转换；
- APP 擦除开始后，取消只停止升级并保留旧激活槽；
- GUI 失败时目标 APP 仍保持非激活；
- Active Record 提交前任何失败都保持旧激活槽；
- Active Record 提交后请求清除失败不能回退激活槽；
- `CHECK_ALREADY_INSTALLED` 匹配相同 `package_id_hash128` 和 `manifest_sha256` 时，不得再次擦写；
- 错误结果记录失败阶段、稳定错误和只用于诊断的 native error。

当前同步 `SpiNor_Erase()` 最坏可阻塞到 5 秒，不满足主循环有界要求。必须提供异步擦除 `start/poll`，或把器件状态轮询提升到可增量执行的 Interface。

### 9.3 已安装包识别

当请求存在时，Update Service 计算并验证完整 `manifest_sha256`，然后比较当前 Active Record：

```text
active.package_id_hash128 == hash128(manifest.package_id)
AND
active.manifest_sha256 == sha256(full_manifest_file)
AND
active.state == ACTIVE_VALID
```

匹配时：

1. 不选择目标槽；
2. 不执行擦除或写入；
3. 进入 `CLEAR_UPDATE_REQUEST`；
4. 清除成功后返回 `SUCCEEDED_ALREADY_INSTALLED`；
5. 清除失败时返回“升级已经生效、请求清理告警”，不得返回安装事务失败。

## 10. Service 划分

### 10.1 Use-case Services

| Service | 形态 | 职责 |
|---|---|---|
| `update_service` | 异步 | 完成请求校验、APP/GUI 原子升级、Active Record 提交和请求清理 |
| `active_validation_service` | 异步 | 增量校验激活 APP/GUI 边界、CRC 和向量表 |
| `recovery_service` | 异步 | 校验备用槽、重建 Active Record 或等待文件系统升级包 |
| `launch_service` | 同步、成功不返回 | 建立 XIP/MPU/Cache 环境并跳转 APP |
| `reset_service` | 同步、成功不返回 | 升级成功后的受控系统复位 |

### 10.2 Capability Services/Modules

| 模块 | 形态 | 职责 |
|---|---|---|
| `boot_control_service` | 同步 | 选择、验证和原子提交 EEPROM Active Record A/B |
| `update_request_service` | 同步 | 严格解析请求、匹配包身份、清理陈旧请求 |
| `manifest_service` | 同步解析 + 增量 Hash | 校验 Manifest SHA 绑定、严格解析 Manifest、提供组件 Hash 和安装元数据 |
| `relocation_service` | 增量 | 校验并应用 APPX 重定位 |
| `slot_policy` | 无状态 | 固定分区、配对、目标槽和范围检查 |
| `version_policy` | 无状态 | APP/GUI 发布版本和防回滚判断 |
| `package_identity` | 无状态/增量 Hash | 生成 `package_id_hash128` 和 `manifest_sha256` |
| `vector_validation` | 无状态 | MSP、Reset Handler 和向量表检查 |
| `checked_arithmetic` | 无状态 | 地址、大小和偏移溢出检查 |

`runtime_service` 不承载升级业务。看门狗周期刷新由 Application 每轮调用或 Platform 管理。

## 11. Interface 设计

Services 只依赖稳定接口，不出现 `FIL`、`FRESULT`、`SD_HandleTypeDef`、`HAL_SD_*`、`USBH_*`、`QSPI_HandleTypeDef` 或 EEPROM Driver 类型。

### 11.1 `package_source_t`

只读升级包接口：

```c
typedef struct
{
    void *context;

    firmware_status_t (*mount)(void *context);
    firmware_status_t (*unmount)(void *context);
    bool (*is_media_present)(void *context);

    firmware_status_t (*open)(
        void *context,
        const char *fixed_path,
        package_file_t *file);

    firmware_status_t (*close)(
        void *context,
        package_file_t *file);

    firmware_status_t (*get_size)(
        void *context,
        package_file_t *file,
        uint32_t *size);

    firmware_status_t (*read_at)(
        void *context,
        package_file_t *file,
        uint32_t offset,
        uint8_t *buffer,
        uint32_t size,
        uint32_t *read_size);
} package_source_t;
```

当前由 `fatfs_sd_package_source_adapter` 实现；未来可由 `fatfs_emmc_package_source_adapter` 实现。

`fixed_path` 只能来自只读产品配置或经过固定文件名映射的 Manifest 结果，不允许透传任意外部路径。

### 11.2 `update_request_store_t`

请求读取和清理接口：

```c
typedef struct
{
    void *context;

    firmware_status_t (*load)(
        void *context,
        uint8_t *buffer,
        uint32_t buffer_size,
        uint32_t *actual_size);

    firmware_status_t (*remove)(void *context);

    firmware_status_t (*sync)(void *context);
} update_request_store_t;
```

接口语义：

- `load` 只读取固定路径 `/boot_update_request.json`；
- 文件不存在返回稳定的 `NOT_FOUND`，不等同于 I/O 错误；
- JSON 解析、`manifest_sha256` 和 `package_id` 比较属于 `update_request_service`，不属于 Adapter；
- 正式 Adapter/存储方案必须满足“只有受信写入者可创建或替换请求”的合同；
- 清除前，Service 必须重新 `load`、解析并确认仍是本次已提交的 `package_id + manifest_sha256`，然后调用 `remove`；
- `sync` 在底层文件系统无法保证时可返回 `NOT_SUPPORTED`，由 Service 按产品策略处理。

当前开发阶段由 `fatfs_update_request_store_adapter` 实现并采用人工信任覆盖。正式版本必须为该 Adapter 增加受信写入约束，或替换为能保证请求不可被未授权修改的存储实现。

### 11.3 `async_block_device_t`

```c
get_info
read
program
erase_start
poll
get_operation_result
cancel（器件允许时）
```

W25Q256 Adapter 实现。擦除操作不得在 Service 中进行无界 Busy Loop。

### 11.4 `xip_controller_t`

```c
enter_memory_mapped_read
exit_memory_mapped
is_memory_mapped
invalidate_mapped_cache
```

该接口由 QSPI Adapter/Platform 实现。

### 11.5 `boot_control_store_t`

```c
read
write_page
is_ready
get_geometry
```

AT24C128AN Adapter 实现。`boot_control_service` 负责 Active Record 双副本格式和事务语义，Adapter 只负责 EEPROM 技术访问。

### 11.6 其他接口

```text
checksum_t          增量 CRC32
sha256_t            增量 SHA-256
watchdog_t          看门狗刷新
system_clock_t      超时和时间
reset_reason_t      稳定复位原因
system_reset_t      受控系统复位
application_jump_t  最终平台跳转
```

## 12. Adapter 和 Composition

### 12.1 当前 Adapter

```text
Adapters/
├── fatfs_sd_package_source_adapter.c
├── fatfs_update_request_store_adapter.c
├── spi_nor_async_block_adapter.c
├── stm32_qspi_xip_adapter.c
├── at24_active_record_store_adapter.c
├── stm32_system_reset_adapter.c
└── stm32_application_jump_adapter.c
```

### 12.2 Composition 示例

```c
static application_t g_application;
static update_service_t g_update_service;
static boot_control_service_t g_boot_control_service;
static update_request_service_t g_update_request_service;

static fatfs_sd_package_source_adapter_t g_package_source_adapter;
static fatfs_update_request_store_adapter_t g_request_store_adapter;
static at24_active_record_store_adapter_t g_active_store_adapter;

static uint8_t g_request_buffer[512];
static uint8_t g_manifest_buffer[16 * 1024];
static uint8_t g_update_io_buffer[4096] __attribute__((aligned(32)));

static const update_service_dependencies_t g_update_dependencies = {
    .package_source = &g_package_source_adapter.interface,
    .request_store = &g_request_store_adapter.interface,
    .boot_control = &g_boot_control_service,
    .manifest = &g_manifest_service,
    .relocation = &g_relocation_service,
    .installer = &g_component_installer,
    .watchdog = &g_watchdog_adapter,
    .clock = &g_clock_adapter
};
```

Composition 是当前 SD 卡实现与未来 eMMC 实现的唯一绑定点。

## 13. XIP 与启动交接

### 13.1 MPU

1. 32 MiB QSPI 总区域：Normal、Read-only、Cacheable、Execute Never；
2. 当前激活的 1 MiB APP 区域：Normal、Read-only、Cacheable、Executable；
3. GUI 和 Reserved 区域保持 Execute Never。

切换激活槽时只改变 1 MiB 可执行覆盖 Region 的基址。

### 13.2 向量表检查

- APP 至少包含完整 Cortex-M7 核心向量；
- MSP 必须 8-byte 对齐并落入允许 SRAM；
- Reset Handler bit 0 必须为 1；
- `(reset_handler & ~1U)` 位于激活 APP 范围和 `app_size` 内；
- `SCB->VTOR` 设置为 APP1 或 APP2 基址；
- 设置 VTOR、MSP 和分支前执行 DSB/ISB。

### 13.3 跳转前清理

1. 停止 Update/Recovery 文件读取；
2. 关闭所有打开文件并卸载文件系统；
3. 停止 SDIO/SDMMC DMA 和中断活动；
4. 禁止中断并停止 SysTick；
5. 禁用并清除全部 NVIC 中断；
6. 退出所有 QSPI 间接操作；
7. 进入稳定 Memory-Mapped Read；
8. Invalidate 相关 D-Cache 和全部 I-Cache；
9. 配置 MPU；
10. 设置 VTOR/MSP 并跳转。

Application Startup 不得关闭 QSPI 时钟、退出 4-byte 模式或重置 QSPI 控制器。若 Application 必须重配系统时钟，相关代码必须位于内部 SRAM/Flash，并保证 QSPI Kernel Clock 连续有效。

## 14. 稳定错误模型

```text
BOOT_ERROR_NONE
BOOT_ERROR_CONTROL_RECORD
BOOT_ERROR_NO_VALID_PAIR
BOOT_ERROR_MEDIA_UNAVAILABLE
BOOT_ERROR_FILESYSTEM_MOUNT
BOOT_ERROR_UPDATE_REQUEST_NOT_FOUND
BOOT_ERROR_UPDATE_REQUEST_IO
BOOT_ERROR_UPDATE_REQUEST_FORMAT
BOOT_ERROR_UPDATE_REQUEST_PACKAGE_MISMATCH
BOOT_ERROR_MANIFEST_FORMAT
BOOT_ERROR_INCOMPATIBLE_PRODUCT
BOOT_ERROR_VERSION_REJECTED
BOOT_ERROR_MANIFEST_HASH
BOOT_ERROR_REQUEST_NOT_TRUSTED
BOOT_ERROR_APP_SOURCE_HASH
BOOT_ERROR_GUI_SOURCE_HASH
BOOT_ERROR_RELOCATION_FORMAT
BOOT_ERROR_RELOCATION_RANGE
BOOT_ERROR_APP_ERASE
BOOT_ERROR_APP_PROGRAM
BOOT_ERROR_APP_TARGET_CRC
BOOT_ERROR_GUI_ERASE
BOOT_ERROR_GUI_PROGRAM
BOOT_ERROR_GUI_TARGET_CRC
BOOT_ERROR_EEPROM_COMMIT
BOOT_ERROR_REQUEST_CLEAR
BOOT_ERROR_XIP_SETUP
BOOT_ERROR_VECTOR_TABLE
BOOT_ERROR_RESET_RETURNED
BOOT_ERROR_INTERNAL
```

结果还应包含非失败告警：

```text
BOOT_WARNING_NONE
BOOT_WARNING_REQUEST_CLEAR_FAILED_AFTER_COMMIT
BOOT_WARNING_ALREADY_INSTALLED_REQUEST_CLEANED
```

规则：

- Active Record 提交前的请求清理错误不应发生，因为不得提前清理；
- Active Record 提交后的请求删除失败属于告警，不改变升级成功结果；
- HAL、FatFs、SDIO/SDMMC、I2C 和 QSPI 原始错误只保存在诊断字段；
- Application 只依据稳定错误和“是否已经提交”做状态决策。

## 15. 静态内存预算

| 对象 | 建议大小 | 所有者 |
|---|---:|---|
| Update Request buffer | 512 B | Composition / Update Request Service |
| Request JSON tokens | 768 B 上限 | Composition |
| Manifest buffer | 16 KiB | Composition |
| Manifest JSON tokens | 4 KiB 上限 | Composition |
| APP/GUI I/O buffer | 4 KiB，32-byte 对齐 | Composition |
| Hash/CRC context | 由实现确定，小于 1 KiB | Service |
| APPX header | 64 B | Update Service |
| Relocation look-ahead | 1 KiB 固定容量 | Update Service |
| EEPROM Active Record buffers | 2 x 256 B | Boot Control Service |

这些缓冲区不得放在函数栈。最终链接 Map 必须检查 DTCM/AXI SRAM 占用和 DMA Cache 一致性。

## 16. 推荐目录与 Target

```text
Application/
  application.c
  application_state.h

Services/
  Common/
  Capability/BootControl/
  Capability/UpdateRequest/
  Capability/Manifest/
  Capability/PackageIdentity/
  Capability/Relocation/
  Capability/SlotPolicy/
  UseCase/Update/
  UseCase/Recovery/
  UseCase/ActiveValidation/
  UseCase/Launch/
  UseCase/Reset/

Interfaces/include/firmware/
  package_source.h
  update_request_store.h
  async_block_device.h
  xip_controller.h
  boot_control_store.h
  system_reset.h
  application_jump.h

Adapters/
  fatfs_sd_package_source_adapter.c
  fatfs_update_request_store_adapter.c
  spi_nor_async_block_adapter.c
  stm32_qspi_xip_adapter.c
  at24_active_record_store_adapter.c
  stm32_system_reset_adapter.c
  stm32_application_jump_adapter.c

Tools/package/
  build_manifest.py
  build_appx.py
  create_update_request.py
```

CMake 依赖保持：

```text
Application -> Services
Services -> Interfaces + Shared
Adapters -> Interfaces + BSP/Platform/Middleware/Generated Glue
Composition -> Application + Services + Adapters
```

`firmware_services` 不得获得 FatFs、SDIO/SDMMC、HAL、BSP 或 Driver include path。

## 17. 验收测试

### 17.1 Host 测试

- 固定分区边界和 APP/GUI 配对；
- Request 文件不存在、空文件、超长、BOM、截断；
- Request 缺字段、重复字段、未知字段、错误类型；
- `requested=false`、错误版本、非法 `package_id`、非法 `manifest_sha256`；
- Request `manifest_sha256` 与实际 Manifest 不一致；
- Request `package_id` 与 SHA 绑定后的 Manifest 不一致；
- Manifest 缺字段、重复字段、未知字段和越界数值；
- Application 侧覆盖正确签名、错误签名、错误 Key ID 和公钥未配置；
- Bootloader 侧确认不存在 Manifest 验签代码路径；
- APP/GUI SHA-256 正确和错误；
- APPX Header CRC、Relocation CRC 和文件长度；
- 重定位未排序、重复、未对齐、越界、未知类型和加法溢出；
- APP1/APP2 目标 CRC 与 Host 工具一致；
- EEPROM A/B 选择、序列号回绕、同序列冲突；
- EEPROM 每个页写和 Commit Marker 写入点的掉电注入；
- SD 文件短读、文件被替换、介质中途移除；
- APP 成功/GUI 失败时不提交；
- EEPROM 提交失败时仍启动旧槽；
- Active Record 提交成功但请求删除失败时不重复安装；
- 已安装相同 `package_id_hash128 + manifest_sha256` 时只清请求；
- 请求清理前被替换为新 package_id 时不得误删；
- Update Service 每个阶段的错误映射；
- Application 状态转换只依赖稳定 Service 结果。

### 17.2 Target 测试

- SDIO/SDMMC 初始化、FatFs 挂载、卸载和重复挂载；
- SD 卡不存在时正常启动延迟有界；
- Recovery 模式插卡后能够识别请求；
- SD 卡写保护导致请求删除失败时，新 Active Record 仍生效；
- 读文件期间移除 SD 卡；
- W25Q256 JEDEC 容量和 `FlashSize=24`；
- APP1、APP2 分别 XIP 启动；
- GUI1、GUI2 Memory-Mapped 读取；
- MPU 阻止 GUI/Reserved 执行；
- I/D Cache 开关和槽切换后一致性；
- QSPI 间接模式与 Memory-Mapped 模式反复切换；
- 擦除、写入和 EEPROM 提交期间随机断电；
- EEPROM 提交后、请求删除前随机断电；
- 请求删除后、系统复位前随机断电；
- IWDG 复位后旧槽可启动或升级可重试；
- Application Startup 不破坏 XIP 时钟；
- 1 MiB APP 和 8 MiB GUI 边界容量。

## 18. 实施顺序

1. 冻结文件系统升级契约 V2、可信请求 JSON Schema、Manifest Schema、CRC 参数和 APPX/Relocation V1；
2. 在 CubeMX 中确认 SDIO/SDMMC、FatFs 和 DMA/Cache 配置；
3. 实现 `fatfs_sd_package_source_adapter` 和 Host Fake；
4. 实现 `fatfs_update_request_store_adapter` 和 `update_request_service`；
5. 实现 Host `build_appx`、Manifest 工具和开发请求生成工具（自动计算 manifest_sha256）；
6. 实现 AT24C128AN Active Record Adapter 和 Boot Control Host Fake；
7. 实现 EEPROM Active Record 双记录 Capability Service；
8. 实现 QSPI 异步擦除与 XIP Controller；
9. 实现 Slot Policy、Manifest、Package Identity 和 Relocation Capability；
10. 实现 Update Service 状态机和掉电注入 Host 测试；
11. 实现 Active Validation、Recovery、Reset 和 Launch Service；
12. 替换现有 Application 转发逻辑为顶层状态机；
13. 在 Composition 绑定 SD 卡 Adapter；
14. 执行 APP1/APP2 真机 XIP、SD 卡移除和随机断电验证；
15. 未来启用 eMMC 时新增 eMMC Adapter，并重复 PackageSource/RequestStore 合同测试。

## 19. 仍需冻结的输入

- AT24C128AN 的板级 I2C 地址、WP 管脚策略和实际页大小；
- CRC-32 最终参数及 STM32 CRC 外设输入字节序；
- Application 侧 ECDSA 公钥的量产烧录介质、Key ID 生命周期和密钥轮换流程；
- 正式请求文件的可信存储/访问控制保证；
- SD 卡所用 FAT 类型、最小容量、扇区大小和写保护策略；
- FatFs `f_sync`、目录同步和重命名行为的产品约束；
- 正常启动文件系统探测超时；
- Recovery 模式介质重试周期；
- APPX Host 工具允许的 ARM relocation 白名单和最大条目数；
- GUI 激活槽信息由 EEPROM、共享 SRAM handoff 还是 Boot API 提供给 Application；
- 未来 eMMC 的 Block Device/FatFs Adapter 和掉电一致性保证；
- 未来 Audio 在 W25Q256 保留区的分区和独立提交策略。
