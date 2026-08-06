# STM32H7 Bootloader 详细设计

> 状态：设计草案 v0.1  
> 依据：`stm32h7_bootloader_business_rules.md` v0.6  
> 范围：APP/GUI 双槽升级、W25Q256 XIP、AT24C128AN 提交记录、USB MSC 升级包。

## 1. 设计结论

本设计固定以下实现方向：

1. Bootloader 位于 STM32H743 内部 Flash；
2. APP1/APP2 直接从 W25Q256 QSPI Memory-Mapped 区域执行；
3. APP1+GUI1 和 APP2+GUI2 构成两个原子发布槽；
4. 升级始终覆盖非激活槽，EEPROM 提交前激活槽不变；
5. 升级介质由同一目录中的 `manifest.json`、`hmi.app.bin`、`hmi.gui.bin` 组成；
6. APP/GUI 共享一个发布版本；未来 Audio 使用独立发布组和独立版本；
7. 升级包只有一个 APPX，APPX 通过安装时重定位适配两个 XIP 基址；
8. AT24C128AN 使用双副本记录和最后提交标记保证掉电安全；
9. 长流程由 `update_service_process()` 增量执行，不使用动态内存；
10. QSPI、FatFs、EEPROM、CRC、签名和跳转均通过 Interface/Adapter 隔离。

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

所有区域均按 4 KiB 擦除边界对齐。Bootloader 必须使用固定表查找区域，不接受 Manifest 提供的物理地址。

### 2.2 QSPI 参数

W25Q256 需要 25 个地址位，因此 STM32 HAL `QSPI_InitTypeDef.FlashSize` 必须设置为 `24`。当前 CubeMX 值 `25` 必须在启用 XIP 前修正。

首版 Memory-Mapped 读取使用现有驱动兼容的保守模式：

- 指令：Fast Read `0x0B`；
- Instruction：1 line；
- Address：4 bytes、1 line；
- Data：1 line；
- Dummy cycles：8；
- DDR：关闭；
- Timeout counter：关闭；
- SIOO：每次发送指令。

后续可以切换 Quad I/O Read，但必须单独验证 QE 位、Dummy cycle、Cache 和复位恢复，不属于首版设计。

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

不允许以下组合：

- APP1 + GUI2；
- APP2 + GUI1；
- 单独提交 APP；
- 单独提交 GUI；
- Manifest 指定任意 Flash 地址；
- EEPROM 提交前跳转目标槽。

`release_version` 属于 APP/GUI 发布组，不分别保存 `app_version` 和 `gui_version`。

## 4. 升级介质

### 4.1 文件集合

三个文件必须位于同一目录：

```text
manifest.json
hmi.app.bin
hmi.gui.bin
```

默认搜索目录建议为 `/firmware`，最终目录名通过只读产品配置冻结。Manifest 中的文件名只能是基础文件名，不允许绝对路径、`..`、路径分隔符或驱动器前缀。

### 4.2 Manifest 解析规则

Manifest 最大长度固定为 16 KiB，使用静态缓冲区。解析器必须：

- 使用 JSON tokenizer/parser，不使用字符串查找；
- 拒绝重复 Key；
- 拒绝未知的顶层和组件字段；
- 拒绝缺失、`null` 或类型错误的生产字段；
- 数值必须为十进制无符号整数且可表示为 `uint32_t`；
- CRC 使用 8 位大写十六进制字符串；
- SHA-256 使用 64 位小写十六进制字符串；
- 文件名最大 31 字节，仅允许 `[A-Za-z0-9._-]`；
- 版本固定为 `major.minor.patch`，每段范围为 `0..65535`，不接受 prerelease/build metadata；
- `components` 必须恰好包含一个 `app` 和一个 `gui`；
- `release_groups` 必须包含原子 `app-gui` 组；
- `target` 只能是 `inactive-app-slot` 或 `inactive-gui-slot`；
- `maximum_image_size_bytes` 必须等于 Bootloader 固定配置，不能扩大分区。

`demo/manifest.json` 中的 `null` 只表示模板占位。打包完成后的生产 Manifest 不允许存在 `null`。

组件校验字段含义固定为：

| 字段 | 覆盖范围 |
|---|---|
| APP `sha256` | 完整 `hmi.app.bin` 文件，包括 APPX Header、Canonical Image 和 Relocation Table |
| APP `source_crc32` | Canonical APP Image 数据，不包括 APPX Header 和 Relocation Table |
| APP `target_crc32.app1/app2` | 应用目标基址重定位后、实际写入 APP 槽的 Image 数据 |
| GUI `sha256` | 完整 `hmi.gui.bin` 文件 |
| GUI `crc32` | 完整 `hmi.gui.bin` 文件 |

### 4.3 Manifest 签名范围

当前模板使用 ECDSA P-256/SHA-256。签名覆盖：

- 除 `signature.value` 外的 Manifest 字段；
- APPX 文件 SHA-256；
- GUI 文件 SHA-256；
- APP1/APP2 重定位后预期 CRC；
- 重定位格式版本。

若继续采用 RFC 8785 JSON Canonicalization，Host 打包工具与 Bootloader 必须使用同一测试向量。密码学库和公钥烧录方式在实现前冻结；未完成签名能力时不得把 CRC 描述为安全认证。

首版签名契约现已冻结：

- 固件只编译 micro-ecc 的 P-256 验签所需纯 C 源码，关闭其它曲线、压缩点和汇编优化；
- ECDSA 签名输入固定为 64-byte 大端 `r || s`，Manifest 中使用严格 RFC 4648 Base64（必须保留规范填充，不接受空白或非零填充位）；
- 公钥固定为 64-byte 未压缩 `X || Y`，由 Composition 在 `Composition_Init()` 前通过显式配置接口注入并复制；
- 配置阶段调用 `uECC_valid_public_key()` 验证曲线点和 Key ID 字符集，验签阶段只接受匹配的 Key ID；
- SHA-256 使用独立、无动态内存的增量实现；
- Canonicalization 采用本 Manifest 的 ASCII、无转义字符串、uint32 十进制整数、布尔、数组和对象子集。对象 Key 按字节序排序，拒绝重复 Key，并从签名输入中移除 `signature.value`；
- 未配置生产公钥时不创建 Manifest verifier，仓库不内置测试私钥或测试公钥作为默认值。

## 5. APPX 文件格式

`hmi.app.bin` 不是普通裸 `.bin`，而是固定小端格式的 `HMI_XIP_APP_V1` 容器：

```text
+----------------------------+
| 64-byte app container hdr  |
+----------------------------+
| canonical APP image bytes  |
+----------------------------+
| relocation entries         |
+----------------------------+
```

### 5.1 APPX 头

所有多字节字段使用 little-endian。

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

### 5.2 精简重定位项

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
```

执行语义：

```text
final_word = canonical_word + target_xip_base
```

规则：

- `target_offset` 必须 4 字节对齐；
- `target_offset + 4 <= image_size`；
- 条目必须严格递增，不允许重复；
- `reserved` 必须为 0；
- 加法必须检查 `uint32_t` 溢出；
- 未知类型立即拒绝整个包；
- 初始 MSP 指向 SRAM，不得重定位；
- Reset Handler、IRQ Handler 和 APP 内部绝对地址必须有对应重定位项；
- MMIO、SRAM 和其他明确的绝对地址不得错误地增加 XIP 基址。

Host 打包工具从带 relocation 信息的 ELF 提取白名单条目并生成该表。Bootloader 不解析 ELF。

### 5.3 流式重定位

APP 使用 4 KiB 对齐 I/O Buffer 流式处理：

1. 读取下一块 Canonical APP；
2. 读取所有落在当前块内的重定位项；
3. 在 RAM Buffer 内应用目标 XIP 基址；
4. 按 W25Q256 256-byte page 边界写入目标槽；
5. 继续下一块；
6. 完成后从 Flash 间接读取并计算最终 CRC；
7. 根据目标槽对比 Manifest 的 `target_crc32.app1` 或 `target_crc32.app2`。

打包工具必须模拟两种目标基址，生成两个预期目标 CRC，但升级包仍只有一份 APPX。

## 6. AT24C128AN 数据布局

### 6.1 容量与布局

AT24C128AN 按 16 KiB 地址空间设计，地址范围 `0x0000..0x3FFF`。页大小和 I2C 地址必须用量产器件数据手册及板级原理图再次确认；首版按 64-byte 页设计。

| EEPROM 地址 | 大小 | 用途 |
|---|---:|---|
| `0x0000` | 256 B | Active Record A |
| `0x0100` | 256 B | Active Record B |
| `0x0200` | 64 B | Update Request A |
| `0x0240` | 64 B | Update Request B |
| `0x0280` | 384 B | Boot metadata 保留 |
| `0x0400` | 1024 B | 未来 Audio control 保留 |
| `0x0800` | 14336 B | 未分配，禁止隐式使用 |

### 6.2 Active Record V1

每份 Active Record 固定 256 字节：

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| `0x00` | 4 | magic = `HBCR` |
| `0x04` | 2 | format_version = 1 |
| `0x06` | 2 | record_size = 256 |
| `0x08` | 4 | sequence |
| `0x0C` | 1 | state |
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

首版 Active Record 的 `state` 只允许 `1 = ACTIVE_VALID`。升级请求和升级过程状态不复用该字段；它们分别保存在 Request Record 和 RAM Service 状态中。`package_id_hash128` 定义为 UTF-8 `package_id` 的 SHA-256 前 16 字节，`manifest_sha256` 为包含最终签名值的完整 Manifest 文件 SHA-256。

### 6.3 Active Record 提交算法

1. 读取 A/B，检查 Magic、版本、范围、Record CRC 和 Commit Marker；
2. 使用模 32-bit 序列号比较选择最新有效副本；
3. 选择另一副本作为写入目标；
4. 先把目标副本 Commit Marker 写为无效值并等待 EEPROM ready；
5. 按页写入 `0x00..0xFB`，每页 ACK polling，禁止固定延时假设；
6. 读回并验证全部内容及 Record CRC；
7. 最后单独写入 Commit Marker；
8. 再次读回 Commit Marker 和关键字段；
9. 只有步骤 8 成功，内存中的 active record 才切换到新副本。

任何一步掉电或失败，旧副本继续有效。若 A/B 序列号相同但内容不同，视为存储冲突并进入恢复。

### 6.4 Update Request

升级请求与 Active Record 分离，Application 不需要改写激活记录。Request 使用两个 64-byte 副本，包含 Magic、序列号、requested、request_reason、Record CRC 和 Commit Marker，提交方式与 Active Record 相同。

更新过程中不持久化每个 Flash 块进度。掉电后：

- Active Record 仍指向旧发布槽；
- Update Request 仍有效；
- Bootloader 重新擦除并从头写非激活槽；
- 最终 Active Record 提交成功后再清除 Request。

该策略避免频繁写 EEPROM，也不需要恢复部分重定位上下文。

## 7. Application 顶层状态机

| 状态 | 入口动作 | 周期动作 | 成功出口 | 失败出口 |
|---|---|---|---|---|
| `STARTUP` | 收集复位原因 | 检查 Composition 初始化 | `SELECT_MODE` | `FAULT` |
| `SELECT_MODE` | 读取 Active/Request Record | 校验状态并选择模式 | `UPDATE`、`VALIDATE_ACTIVE` | `RECOVERY` |
| `VALIDATE_ACTIVE` | 启动激活槽校验 | 增量 CRC APP/GUI | `LAUNCH` | `RECOVERY` |
| `UPDATE` | 启动 Update Service | 调用 `update_service_process()` | `VALIDATE_ACTIVE` | `RECOVERY` 或旧槽 `LAUNCH` |
| `RECOVERY` | 检查另一完整槽或 USB | 恢复/重启更新 | `VALIDATE_ACTIVE` | 保持等待或 `FAULT` |
| `LAUNCH` | 启动 XIP Launch Service | 不返回 | Application | `FAULT` |
| `FAULT` | 记录稳定错误 | 喂狗策略由产品配置决定 | 受控复位/恢复 | 保持 |

Application 只根据 Service 状态和稳定错误做转换，不包含 FatFs、QSPI、CRC、EEPROM 或 HAL 技术步骤。

## 8. Update Service 状态机

### 8.1 外部状态

```text
IDLE
RUNNING
SUCCEEDED
FAILED
CANCELLED（仅擦除目标槽前允许）
```

### 8.2 内部阶段

```text
WAIT_MEDIA
OPEN_MANIFEST
PARSE_MANIFEST
VALIDATE_MANIFEST
VERIFY_SIGNATURE
SELECT_TARGET
VERIFY_APP_SOURCE
VERIFY_GUI_SOURCE
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

- 验证 Manifest、签名、APPX Header、重定位表和两个源文件 Hash 后才允许擦除目标 APP；
- 每次 `process()` 最多执行一次短文件读取、一个 4 KiB 擦除单元、一个 4 KiB 数据块或一个状态转换；
- APP 擦除开始后，取消只停止升级并保留旧激活槽，不得激活目标槽；
- GUI 失败时目标 APP 仍保持非激活；
- Active Record 提交前任何失败都保持旧激活槽；
- Active Record 提交后清 Request 失败不能回退激活槽，下次启动应识别相同 package_id 并清理重复请求；
- 错误结果记录失败阶段、稳定错误和只用于诊断的 native error。

当前同步 `SpiNor_Erase()` 最坏可阻塞到 5 秒，不满足主循环有界要求。实现阶段必须提供异步擦除 `start/poll`，或把器件状态轮询提升到可增量执行的 Interface；仅把整个 4 KiB 同步擦除放入 `process()` 不作为最终设计。

## 9. Service 划分

### 9.1 Use-case Services

| Service | 形态 | 职责 |
|---|---|---|
| `update_service` | 异步 | 完成 APP/GUI 原子升级事务 |
| `active_validation_service` | 异步 | 增量校验激活 APP/GUI 边界、CRC 和向量表 |
| `recovery_service` | 异步 | 校验备用槽、重建 Active Record 或等待升级介质 |
| `launch_service` | 同步、成功不返回 | 建立 XIP/MPU/Cache 环境并跳转 APP |

### 9.2 Capability Services/Modules

| 模块 | 形态 | 职责 |
|---|---|---|
| `boot_control_service` | 同步 | 选择、验证和原子提交 EEPROM 双记录 |
| `manifest_service` | 同步解析 + 增量文件 Hash | 严格解析并验证 Manifest |
| `relocation_service` | 增量 | 校验并应用 APPX 重定位 |
| `slot_policy` | 无状态 | 固定分区、配对、目标槽和范围检查 |
| `version_policy` | 无状态 | APP/GUI 发布版本和防回滚判断 |
| `vector_validation` | 无状态 | MSP、Reset Handler 和向量表检查 |
| `checked_arithmetic` | 无状态 | 地址、大小和偏移溢出检查 |

`runtime_service` 不承载升级业务。看门狗周期刷新应作为系统健康能力由 Application 每轮调用，或由 Platform 管理；不得把技术性的 `runtime_service` 当作顶层业务用例。

## 10. Interface 设计

Services 只依赖以下稳定接口：

### 10.1 `package_source_t`

```c
mount, unmount
open, close
get_size
read_at
is_media_present
```

FatFs/USB Host Adapter 实现该接口。Service 不读取 `FIL`、`FRESULT` 或 USBH 类型。

### 10.2 `async_block_device_t`

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

### 10.3 `xip_controller_t`

```c
enter_memory_mapped_read
exit_memory_mapped
is_memory_mapped
invalidate_mapped_cache
```

该接口由 QSPI Adapter/Platform 实现，公共类型中不得出现 `QSPI_HandleTypeDef`。

### 10.4 `boot_control_store_t`

```c
read
write_page
is_ready
get_geometry
```

AT24C128AN Adapter 实现。`boot_control_service` 负责双副本格式和事务语义，Adapter 只负责 EEPROM 技术访问。

### 10.5 其他接口

```text
checksum_t          增量 CRC32
hash_verifier_t     增量 SHA-256 / ECDSA 验证
watchdog_t          看门狗刷新
system_clock_t      超时和时间
reset_reason_t      稳定复位原因
application_jump_t  最终平台跳转
```

## 11. XIP 与启动交接

### 11.1 MPU 设计

使用高优先级覆盖方式：

1. 32 MiB QSPI 总区域：Normal、Read-only、Cacheable、Execute Never；
2. 当前激活的 1 MiB APP 区域：Normal、Read-only、Cacheable、Executable；
3. GUI 和 Reserved 区域保持 Execute Never。

切换激活槽时只改变 1 MiB 可执行覆盖 Region 的基址。

### 11.2 向量表检查

- APP 至少包含完整 Cortex-M7 核心向量；
- MSP 必须 8-byte 对齐并落入允许的 SRAM 区域；
- Reset Handler bit 0 必须为 1；
- `(reset_handler & ~1U)` 必须位于激活 APP 范围和 `app_size` 内；
- `SCB->VTOR` 设置为 APP1 或 APP2 基址；
- 设置 VTOR、MSP 和分支前执行 DSB/ISB。

### 11.3 Cache 和外设状态

跳转前：

1. 禁止中断；
2. 停止 SysTick；
3. 禁用并清除全部 NVIC 中断；
4. 退出所有仍在使用 QSPI 的 Bootloader 操作；
5. 进入稳定 Memory-Mapped Read；
6. Invalidate QSPI 地址范围相关 D-Cache 和全部 I-Cache；
7. 配置 MPU；
8. 设置 VTOR/MSP 并跳转。

Application Startup 不得执行会关闭 QSPI 时钟、退出 4-byte 模式或重置 QSPI 控制器的通用 `SystemInit()`。若 Application 必须重配系统时钟，重配置代码及所需常量必须位于内部 SRAM/Flash，并保证 QSPI Kernel Clock 连续有效。

## 12. 稳定错误模型

```text
BOOT_ERROR_NONE
BOOT_ERROR_CONTROL_RECORD
BOOT_ERROR_NO_VALID_PAIR
BOOT_ERROR_MEDIA_UNAVAILABLE
BOOT_ERROR_MANIFEST_FORMAT
BOOT_ERROR_INCOMPATIBLE_PRODUCT
BOOT_ERROR_VERSION_REJECTED
BOOT_ERROR_SIGNATURE
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
BOOT_ERROR_XIP_SETUP
BOOT_ERROR_VECTOR_TABLE
BOOT_ERROR_INTERNAL
```

HAL、FatFs、USBH、I2C 和 QSPI 原始错误只保存在诊断字段，不参与 Application 状态决策。

## 13. 静态内存预算

| 对象 | 建议大小 | 所有者 |
|---|---:|---|
| Manifest buffer | 16 KiB | Composition |
| JSON tokens | 4 KiB 上限 | Composition |
| APP/GUI I/O buffer | 4 KiB，32-byte 对齐 | Composition |
| Hash/CRC context | 由实现确定，小于 1 KiB | Service |
| APPX header | 64 B | Update Service |
| Relocation look-ahead | 1 KiB 固定容量 | Update Service |
| EEPROM active record buffers | 2 x 256 B | Boot Control Service |
| EEPROM request buffers | 2 x 64 B | Boot Control Service |

Manifest、I/O 和 relocation buffer 不得放在函数栈。最终链接 Map 必须检查 DTCM/AXI SRAM 占用。

## 14. 推荐目录与 Target

```text
Application/
  application.c
  application_state.h

Services/
  Common/
  Capability/BootControl/
  Capability/Manifest/
  Capability/Relocation/
  Capability/SlotPolicy/
  UseCase/Update/
  UseCase/Recovery/
  UseCase/ActiveValidation/
  UseCase/Launch/

Interfaces/include/firmware/
  package_source.h
  async_block_device.h
  xip_controller.h
  boot_control_store.h
  application_jump.h

Adapters/
  fatfs_package_source_adapter.c
  spi_nor_async_block_adapter.c
  stm32_qspi_xip_adapter.c
  at24_boot_control_adapter.c
  stm32_application_jump_adapter.c

Tools/package/
  build_manifest.py
  build_appx.py
```

CMake 依赖保持：

```text
Application -> Services
Services -> Interfaces + Shared
Adapters -> Interfaces + BSP/Platform/Middleware
Composition -> Application + Services + Adapters
```

## 15. 验收测试

### 15.1 Host 测试

- 固定分区边界和 APP/GUI 配对；
- Manifest 缺字段、重复字段、未知字段和越界数值；
- APPX Header CRC、Relocation CRC 和文件长度；
- 重定位未排序、重复、未对齐、越界、未知类型和加法溢出；
- APP1/APP2 目标 CRC 与 Host 打包工具一致；
- EEPROM A/B 选择、序列号回绕、同序列冲突；
- EEPROM 每个页写和 Commit Marker 写入点的掉电注入；
- USB 断开、短读、文件被替换；
- APP 成功/GUI 失败时不提交；
- EEPROM 提交失败时仍启动旧槽；
- 提交成功但清 Request 失败时不重复安装；
- Update Service 每个阶段的错误映射；
- Application 状态转换只依赖稳定 Service 结果。

### 15.2 Target 测试

- W25Q256 JEDEC 容量和 `FlashSize=24`；
- APP1、APP2 分别 XIP 启动；
- GUI1、GUI2 Memory-Mapped 读取；
- MPU 阻止 GUI/Reserved 执行；
- I/D Cache 开关和槽切换后一致性；
- QSPI 间接模式与 Memory-Mapped 模式反复切换；
- 擦除、写入和 EEPROM 提交期间随机断电；
- IWDG 复位后旧槽可启动或升级可重试；
- Application Startup 不破坏 XIP 时钟；
- 1 MiB APP 和 8 MiB GUI 边界容量。

## 16. 实施顺序

1. 冻结 Manifest Schema、CRC 参数和 APPX/Relocation V1；
2. 实现 Host `build_appx` 与 Manifest 生成工具，并用两个目标基址生成测试向量；
3. 实现 AT24C128AN Adapter 和 Boot Control Host Fake；
4. 实现 EEPROM 双记录 Capability Service；
5. 实现 QSPI 异步擦除与 XIP Controller；
6. 实现 Slot Policy、Manifest 和 Relocation Capability；
7. 实现 Update Service 骨架及掉电注入 Host 测试；
8. 实现 Active Validation、Recovery 和 Launch Service；
9. 替换现有 Application 转发逻辑为顶层状态机；
10. 最后接入 Composition，并执行 APP1/APP2 真机 XIP 验证。

## 17. 仍需冻结的输入

- AT24C128AN 的板级 I2C 地址、WP 管脚策略和实际页大小；
- CRC-32 最终参数及 STM32 CRC 外设输入字节序；
- ECDSA 公钥的量产烧录介质、Key ID 生命周期和密钥轮换流程（固件注入接口已冻结）；
- USB 升级目录；
- APPX Host 工具允许的 ARM relocation 白名单和最大条目数；
- GUI 激活槽信息由 EEPROM、共享 SRAM handoff 还是 Boot API 提供给 Application；
- 未来 Audio 在 W25Q256 保留区的分区和独立提交策略。
