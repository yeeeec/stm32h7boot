# STM32H7 Bootloader 详细设计

> 版本：v0.4。文件名为兼容旧链接保留；请求文件只负责触发。

## 1. 设计结论

- APP1+GUI1 与 APP2+GUI2 成对交替；
- APP 从 QSPI XIP 运行；升级包保留裸 APP，Manifest V2 描述外置重定位表；
- EEPROM 只保存 Active Record A/B；
- 请求文件只按存在性触发，不解析内容、不做认证；
- Manifest Service 只做严格 Schema 解析、字段校验和 SHA-256；
- Bootloader 不执行 Manifest 验签，不装配公钥或签名验证器；
- Application 持有升级、提交、清理、恢复、启动和复位业务状态机；
- Update Service 只提供 Prepare/Install，返回未提交候选；
- Recovery Service 只扫描、验证、选择并返回未提交候选；
- 所有长流程静态分配、增量执行。

## 2. 固定地址与容量

### 2.1 W25Q256

| 区域 | Flash 偏移 | CPU 地址 | 大小 | 用途 |
|---|---:|---:|---:|---|
| APP1 | `0x000000` | `0x90000000` | 1 MiB | pair 1 XIP APP |
| APP2 | `0xA00000` | `0x90A00000` | 1 MiB | pair 2 XIP APP |
| GUI1 | `0x200000` | `0x90200000` | 8 MiB | pair 1 GUI |
| GUI2 | `0xC00000` | `0x90C00000` | 8 MiB | pair 2 GUI |
| RESERVED | `0x1400000` | `0x91400000` | 12 MiB | 禁止隐式使用 |

全部按 4 KiB 擦除边界对齐。物理地址只来自 `slot_policy` 固定表。
`0x100000..0x200000` 与 `0xB00000..0xC00000` 是配对隔离间隙，不得分配。

W25Q256 需要 25 个地址位，HAL `FlashSize` 应为 24。初始 Memory-Mapped 读取使用 Fast Read `0x0B`、4-byte 地址、8 dummy cycles、单线 SDR；切换 Quad 模式需另行验证。

### 2.2 AT24C128AN

| EEPROM 地址 | 大小 | 用途 |
|---|---:|---|
| `0x0000` | 256 B | Active Record A |
| `0x0100` | 256 B | Active Record B |
| `0x0200..0x3FFF` | 15872 B | 保留，当前不得隐式使用 |

EEPROM 不保存 Update Request、请求状态、升级阶段或逐块进度。

## 3. 文件系统合同

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    ├── hmi.app.reloc.bin
    └── hmi.gui.bin
```

路径由 Composition 配置冻结。请求文件内容完全不读取；存在即触发。Application 调用 `is_media_present/mount/exists/remove/unmount`，Update Service 只调用 `open/get_size/read_at/close`。

发布顺序是四个发布文件全部写入并关闭后，最后创建请求。清理顺序是 Active Record 提交成功后再删除请求；删除失败不回退。

## 4. Manifest

Manifest 最大 16 KiB，使用静态缓冲区与 192 个 JSON token。解析器必须：

- 拒绝重复 Key、未知字段、缺失字段、错误类型和越界数字；
- 固定产品 `HMI`、硬件 `STM32H743-W25Q256`；
- `components` 恰好为 APP、GUI，固定文件名和固定 inactive target；
- 文件名仅接受冻结值，不透传路径；
- 版本三段各为 `uint16_t`；
- CRC 使用冻结的 8 位十六进制表示，SHA-256 为 64 位小写十六进制；
- 计算完整原始 Manifest 文件 SHA-256；
- 计算 `package_id` UTF-8 SHA-256 前 16 字节。

签名对象若属于冻结 Schema，只作为不透明包字段通过结构成员检查；Bootloader 不解码签名、不重建签名输入、不验签，也不输出“签名有效”结论。

组件字段覆盖范围：

| 字段 | 覆盖范围 |
|---|---|
| APP `sha256` | 完整裸 APP 文件 |
| APP `source_crc32` | 完整裸 APP 文件 |
| APP `target_crc32.app1/app2` | 重定位后写入对应目标槽的数据 |
| APP `relocation.crc32` | 完整外置重定位表 |
| GUI `sha256` | 完整 GUI 文件 |
| GUI `crc32` | 完整 GUI 文件 |

## 5. 裸 APP + 外置重定位表 V2

```text
hmi.app.bin          = 原始链接输出，不添加包头
hmi.app.reloc.bin    = 严格递增的 8-byte 重定位条目
manifest.json        = 大小、入口、链接地址、Hash/CRC、表数量和表 CRC
```

### 5.1 Manifest V2 APP 字段

`format` 固定为 `raw-xip-reloc-v2`，`file_size_bytes` 必须等于
`image_size_bytes`，`link_address` 固定为 `0x90000000`。`entry_offset`
必须与裸 APP Reset Handler 去除 Thumb 位后的相对偏移一致。重定位对象固定
文件名 `hmi.app.reloc.bin`，并给出 `count`、`crc32` 和
`hmi-reloc-v1` 格式。

### 5.2 Relocation

```c
typedef struct
{
    uint32_t target_offset;
    uint16_t type;
    uint16_t reserved;
} relocation_entry_t;
```

V1 只允许 `ABS32_ADD_XIP_BASE`：
`final_word = raw_word - link_address + target_xip_base`。条目必须严格递增、
4-byte 对齐、不重复、不越界、reserved 为 0；第一项必须是 Reset Handler
所在的偏移 4。MSP、MMIO 和 SRAM 地址不得重定位。Bootloader 不解析 ELF；
Host 工具只从带 `--emit-relocs` 的 ELF 导出 `R_ARM_ABS32` XIP 白名单并生成
两个目标 CRC。

## 6. Active Record V1

每份记录固定 256 byte：

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| `0x00` | 4 | magic `HBCR` |
| `0x04` | 2 | format_version `1` |
| `0x06` | 2 | record_size `256` |
| `0x08` | 4 | sequence |
| `0x0C` | 1 | state `ACTIVE_VALID` |
| `0x0D` | 1 | active_pair `1/2` |
| `0x0E` | 2 | flags `0` |
| `0x10` | 6 | release major/minor/patch |
| `0x16` | 2 | reserved `0` |
| `0x18` | 4 | build_number |
| `0x1C` | 4 | app_size |
| `0x20` | 4 | app_crc32 |
| `0x24` | 4 | gui_size |
| `0x28` | 4 | gui_crc32 |
| `0x2C` | 16 | package_id_hash128 |
| `0x3C` | 32 | manifest_sha256 |
| `0x5C` | 156 | reserved `0xFF` |
| `0xF8` | 4 | record_crc32，覆盖 `0x00..0xF7` |
| `0xFC` | 4 | Commit Marker `COMT` |

记录不保存裸地址。`active_pair` 通过固定表映射。

提交算法：

1. 读取并校验 A/B；
2. 按 RFC 1982 32-bit 序列选择最新有效副本；
3. 选另一副本为目标并先写无效 Marker；
4. 分页写主体并轮询 ready；
5. 读回、校验内容与 CRC；
6. 最后单独写 Commit Marker；
7. 再次读回校验后提交完成。

同序列但内容不同，或序列相差恰好半周期，视为冲突。Boot Control Service 还允许 Recovery 分 pair 查询保留记录，但不替 Application 决定提交。恢复提交必须保留与候选完全匹配的来源副本，并原子覆盖另一副本，避免掉电破坏唯一已验证记录。

## 7. Application 状态机

当前 Application 编排顺序：

```text
LoadActive
├─ 无可选记录 -> RecoveryStart
└─ 有记录 -> WaitMedia
              -> Mount -> CheckRequest
              -> Prepare -> DecidePackage
              -> Install -> Commit
              -> RemoveRequest -> Unmount -> Reset

无请求/升级失败 -> ValidateActive -> Launch
ValidateActive 失败 -> Recovery -> Commit -> ValidateActive -> Launch
```

关键业务变量：

- `active_record`：当前已提交记录；
- `candidate_record`：Update/Recovery 返回的未提交候选；
- `after_commit`：升级候选进入清理/复位，恢复候选进入校验/启动；
- `recovery_attempted`：限制恢复失败循环；
- `media_mounted` 与 `after_unmount`：保证启动/复位前释放介质。

Application 独占：请求、版本/陈旧决策、跨 Service 事务、提交时机、清理、恢复入口、启动与复位。

## 8. Update Service

公共生命周期分两段：

```text
PrepareStart -> Process* -> GetManifest
InstallStart(active_record) -> Process* -> GetCandidate
```

Prepare 内部阶段只包括打开、读取、关闭和解析 Manifest。Application 在两段之间执行陈旧/版本策略。

Install 内部阶段包括：

```text
SELECT_TARGET
-> OPEN/PREPARE/HASH/VALIDATE APP + RELOCATIONS
-> OPEN/PREPARE/HASH GUI
-> ERASE/PROGRAM/VERIFY APP
-> ERASE/PROGRAM/VERIFY GUI
-> RETURN CANDIDATE
```

Update Service 不执行介质挂载、请求探测/删除、版本策略、EEPROM 提交或系统复位。每次 `Process()` 最多推进一个文件块、一次擦除/编程启动或轮询、一次目标读取或一个状态转换。

## 9. Recovery Service

Recovery 依赖：

- `recovery_candidate_load_fn`：按固定 pair 加载保留 Active Record；
- `active_validation_service`：校验候选对应 APP/GUI。

流程为 pair 1 load/validate、pair 2 load/validate、按有效性与序列选择候选并返回。它不提交 EEPROM、不启动、不复位。Application 提交恢复候选后再次校验并启动。

## 10. Service 与 Interface

| 模块 | 职责 |
|---|---|
| `manifest_service` | 严格解析与 SHA-256/包 ID Hash |
| `update_service` | Prepare/Install、返回候选 |
| `boot_control_service` | Active Record A/B 机制 |
| `active_validation_service` | APP/GUI 边界、CRC、向量校验 |
| `recovery_service` | 候选扫描、验证和选择 |
| `launch_service` | XIP 与最终跳转 |
| `slot_policy/version_policy/vector_validation` | 无状态规则 |

主要接口：

- `package_source_t`：介质、挂载、固定文件读取、存在性和删除；
- `async_block_device_t`：外部 Flash read/program/erase/poll；
- `boot_control_store_t`：EEPROM geometry/read/write_page/is_ready；
- `hash_provider_t`：纯 reset/update/finish SHA-256，不含签名验证；
- `checksum_t`：CRC32；
- `xip_controller_t`：Memory-Mapped 控制；
- `application_jump_t`：最终跳转；
- `system_reset_t`：Application 请求整机复位。

不存在 `update_request_store_t` 或 EEPROM Update Request API。

## 11. Adapter 与 Composition

当前 Adapter：FatFs Package Source、SPI NOR Block、AT24 Boot Control、STM32 QSPI XIP、Cortex-M Jump、STM32 System Reset、Clock、Watchdog 与 Log。

Composition：

1. 创建所有 Adapter、Service 和静态缓冲区；
2. 绑定 Manifest/Update 的纯 Hash provider；
3. 将 Boot Control 的 pair 查询包装成 Recovery candidate loader；
4. 将 Package Source 与 System Reset 注入 Application；
5. 不向 `main.c` 暴露 Services 类型。

依赖方向：

```text
Application -> Services + Interfaces
Services -> Interfaces + Shared
Adapters -> Interfaces + Platform/BSP/Driver/Middleware
Composition -> Application + Services + Adapters
```

Application 不直接依赖 Platform；Services 不获得 HAL/FatFs/BSP/Driver include path。

## 12. XIP 启动交接

- QSPI 32 MiB 默认 Read-only、Cacheable、XN；激活 APP 的 1 MiB 覆盖区允许执行；
- MSP 8-byte 对齐并落入允许 SRAM；Reset Handler bit0 为 1 且位于 `app_size`；
- 启动前无打开文件、无介质操作、无间接 QSPI 操作；
- 进入稳定 Memory-Mapped，处理 I/D Cache 与 MPU；
- 禁止并清除中断、停止 SysTick，设置 VTOR/MSP，执行 DSB/ISB 后跳转；
- Application Startup 不得破坏 QSPI 时钟或退出 4-byte/Memory-Mapped 状态。

## 13. 错误模型

Service 使用 `firmware_status_t + service_run_state_t + service_result_t`。`boot_error_t` 是稳定领域错误；原生 HAL/FatFs/Driver 错误只进入 `native_error`。

请求删除失败由 Application 作为提交后清理失败忽略，不写回 Service 安装结果，不回退，不改变升级成功并复位的动作。

## 14. 静态内存

| 对象 | 当前预算 | 所有者 |
|---|---:|---|
| Manifest buffer | 16 KiB | Composition |
| Manifest token workspace | ≤4 KiB | Manifest Service |
| APP/GUI I/O buffer | 4 KiB，32-byte 对齐 | Composition |
| Relocation raw buffer | `128 * 8` B | Composition |
| Parsed relocation entries | 128 entries | Composition |
| Active Record write/verify | 2×256 B | Boot Control Service |

无请求 buffer、请求 token 或验签公钥/签名工作区。大缓冲区不得位于函数栈。

## 15. 验收测试

Host：Manifest 严格解析但不调用签名验证；Update Prepare/Install 生命周期；Application 提交/清理失败容忍/复位编排；Recovery 候选提交/再校验/启动；Active Record A/B 掉电、序列回绕与 per-pair 查询；原始 APP/重定位表边界；相同发布包不 Install。

Target：SD 缺失/移除/写保护；W25Q256 几何与 XIP；APP1/APP2 启动；随机掉电覆盖擦写、EEPROM 提交、提交后删除前和删除后复位前；请求删除失败后相同包不重写；IWDG 和 Cache/MPU 一致性。

最终 ELF 必须不含 `uECC`、`MicroEcc` 或 `verify_signature` 符号。
