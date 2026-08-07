# STM32H7 Bootloader 详细设计

> 状态：Detailed Design v1  
> 依据：`stm32h7_bootloader_business_rules.md` 与 `stm32h7_firmware_release_package_contract.md`  
> 目标：实现固定 QSPI Runtime + 文件系统发布源 + Trusted Request + EEPROM A/B 元数据。

---

## 1. 设计结论

1. Bootloader 位于 STM32H743 内部 Flash；
2. Application 固定从 W25Q256 `0x90000000` XIP；
3. TouchGFX GUI 固定映射到 `0x90200000`；
4. W25Q256 当前只有一套 Runtime；
5. 发布包来自文件系统；
6. `hmi.app.bin`、`hmi.gui.bin` 为 RAW Binary；
7. 无 APPX、无 relocation；
8. EEPROM A/B Active Record 保留；
9. Active Record V2 不再包含 pair/slot；
10. 所有大文件与 Flash 操作增量执行；
11. Bootloader 不执行 Manifest 签名验证。

---

## 2. 固定地址

### 2.1 W25Q256

```c
#define BOOT_QSPI_MMAP_BASE       0x90000000UL

#define BOOT_APP_FLASH_OFFSET     0x000000UL
#define BOOT_APP_RUNTIME_BASE     0x90000000UL
#define BOOT_APP_RUNTIME_SIZE     0x00100000UL

#define BOOT_GUI_FLASH_OFFSET     0x00200000UL
#define BOOT_GUI_RUNTIME_BASE     0x90200000UL
#define BOOT_GUI_RUNTIME_SIZE     0x00800000UL
```

当前保留：

```text
0x100000..0x1FFFFF
0xA00000..0x1FFFFFF
```

不得隐式当作第二运行槽或 rollback slot。

---

## 3. 文件系统 Adapter

### 3.1 `package_source_t`

负责：

```text
is_media_present   (可移除介质构建需要)
mount
unmount
open fixed package file
close
get_size
read_at
```

Package Source 为只读发布包接口。

建议不把 request clear 混入 Package Source。

### 3.2 `update_request_store_t`

定义窄能力：

```c
typedef struct
{
    uint32_t format_version;
    bool requested;
    char package_id[PACKAGE_ID_MAX];
    uint8_t manifest_sha256[32];
} update_request_t;

typedef struct
{
    void *context;

    firmware_status_t (*load)(
        void *context,
        update_request_t *request);

    firmware_status_t (*clear)(
        void *context);
} update_request_store_t;
```

Adapter 固定知道：

```text
/boot_update_request.json
```

它不接受调用者传任意路径。

开发构建：

```text
fatfs_sd_package_source_adapter
fatfs_update_request_store_adapter
```

正式构建：

```text
emmc_package_source_adapter
emmc_update_request_store_adapter
```

如果 eMMC 仍使用 FatFs，可共用通用 FatFs volume backend。

---

## 4. Manifest Service

职责：

1. 读取完整 raw Manifest；
2. 计算 raw bytes SHA-256；
3. 严格 Schema 解析；
4. 校验产品/硬件；
5. 校验 component 文件名；
6. 校验 component size；
7. 解码 APP/GUI SHA-256；
8. 计算 `package_id_hash128`；
9. 返回 `validated_manifest_t`。

不负责：

- ECDSA；
- 公钥；
- Request trust；
- Runtime 擦写；
- 版本决策。

建议：

```c
typedef struct
{
    release_version_t release;
    uint32_t build_number;

    char package_id[PACKAGE_ID_MAX];
    uint8_t package_id_hash128[16];

    uint8_t manifest_sha256[32];

    uint32_t app_size;
    uint8_t app_sha256[32];

    uint32_t gui_size;
    uint8_t gui_sha256[32];

    version_t minimum_bootloader_version;
} validated_manifest_t;
```

---

## 5. Active Record V2

EEPROM 继续使用两个 256-byte record：

```text
A = 0x0000..0x00FF
B = 0x0100..0x01FF
```

### 5.1 Binary Layout

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | magic `HBCR` |
| `0x04` | 2 | format_version = `2` |
| `0x06` | 2 | record_size = `256` |
| `0x08` | 4 | sequence |
| `0x0C` | 1 | state = `ACTIVE_VALID` |
| `0x0D` | 1 | flags |
| `0x0E` | 2 | reserved |
| `0x10` | 6 | release major/minor/patch (`uint16_t` each) |
| `0x16` | 2 | reserved |
| `0x18` | 4 | build_number |
| `0x1C` | 4 | app_size |
| `0x20` | 4 | gui_size |
| `0x24` | 16 | package_id_hash128 |
| `0x34` | 32 | manifest_sha256 |
| `0x54` | 32 | app_sha256 |
| `0x74` | 32 | gui_sha256 |
| `0x94` | 100 | reserved, `0xFF` |
| `0xF8` | 4 | record_crc32 over `0x00..0xF7` |
| `0xFC` | 4 | Commit Marker `COMT` |

删除 V1：

```text
active_pair
```

Record 不保存 Runtime 地址，因为地址为编译期固定产品配置。

### 5.2 V1 兼容

不得把旧 V1 EEPROM Record 静默解释成 V2。

若产品已经部署 V1，必须另行定义显式 migration。

当前开发阶段可以：

```text
V1 -> unsupported -> require re-provision
```

具体由产品发布状态决定。

---

## 6. Active Record 原子提交

保持 A/B 双副本算法：

1. 读取并结构校验 A/B；
2. 使用 RFC 1982 32-bit sequence 选择最新有效记录；
3. 选择另一副本作为写目标；
4. 先写无效 Commit Marker；
5. 分页写 record body；
6. 等待 EEPROM ready；
7. 读回 body；
8. 校验字段与 `record_crc32`；
9. 最后单独写 `COMT`；
10. 再次读回完整验证；
11. 返回 commit success。

任何掉电只能留下：

```text
old valid record
```

或：

```text
new complete valid record
```

---

## 7. Application 顶层状态

建议：

```text
STARTUP
-> UPDATE_CHECK
   -> UPDATE
   -> VALIDATE_RUNTIME

UPDATE success
-> RESET

VALIDATE_RUNTIME success
-> LAUNCH

VALIDATE_RUNTIME failure
-> RECOVERY
   -> UPDATE retry if authorized source exists
   -> FAULT
```

当前 `RECOVERY` 不扫描 QSPI pair。

---

## 8. Update Service API

建议保留两段：

```text
Prepare
Install
```

### 8.1 Prepare

```c
service_status_t update_service_prepare_start(
    update_service_t *service,
    const update_request_t *request);
```

内部：

```text
OPEN MANIFEST
-> HASH RAW MANIFEST
-> COMPARE REQUEST HASH
-> PARSE MANIFEST
-> COMPARE PACKAGE_ID
-> RETURN VALIDATED MANIFEST
```

版本策略仍由 Application 在 Prepare 与 Install 之间决定。

### 8.2 Install

不再需要：

```text
active_record -> select inactive target
```

建议：

```c
service_status_t update_service_install_start(
    update_service_t *service);
```

或只传入经 Application 接受的 policy result。

---

## 9. Update Service 状态机

建议：

```text
IDLE

SOURCE_APP_OPEN
SOURCE_APP_HASH
SOURCE_APP_VERIFY

SOURCE_GUI_OPEN
SOURCE_GUI_HASH
SOURCE_GUI_VERIFY

APP_ERASE
APP_PROGRAM
APP_READBACK_HASH
APP_TARGET_VERIFY

GUI_ERASE
GUI_PROGRAM
GUI_READBACK_HASH
GUI_TARGET_VERIFY

BUILD_RECORD_CANDIDATE
SUCCEEDED

FAILED
```

第一次 `APP_ERASE` 前必须已经完成：

```text
APP source full SHA
GUI source full SHA
```

删除旧阶段：

```text
SELECT_TARGET
LOAD_RELOCATION
VALIDATE_RELOCATION
APPLY_RELOCATION
```

---

## 10. 流式 I/O

建议：

```text
I/O buffer = 4 KiB
alignment  = 32 bytes
```

Source Hash：

```text
read_at 4 KiB
 -> hash_update
 -> next
```

W25Q256 program：

```text
4 KiB source block
 -> program as 256-byte page operations
 -> poll async device
```

目标回读 SHA：

```text
W25Q256 indirect read 4 KiB
 -> hash_update
```

在 Memory-Mapped 与 indirect 模式切换时必须统一处理 Cache/XIP 状态。

---

## 11. `async_block_device_t`

建议能力：

```text
read
erase_start
program_start
poll
geometry
```

语义：

- erase unit = 4 KiB；
- program page = 256 B；
- 所有范围必须 checked arithmetic；
- 不允许越过固定 Runtime region；
- Service 不传 CPU XIP 地址给 block device；
- block device 使用 W25Q256 physical offset。

---

## 12. Runtime Layout

旧 `slot_policy` 删除。

替换为无状态 `runtime_layout`：

```c
typedef struct
{
    uint32_t app_offset;
    uint32_t app_max_size;
    uint32_t app_xip_base;

    uint32_t gui_offset;
    uint32_t gui_max_size;
    uint32_t gui_mmap_base;
} boot_runtime_layout_t;
```

当前只有一个常量实例。

它不进行“目标选择”。

---

## 13. Active Validation Service

输入：

```text
Active Record V2
Fixed Runtime Layout
Read/Hash capability
```

验证：

1. Record 有效；
2. `app_size <= 1 MiB`；
3. `gui_size <= 8 MiB`；
4. APP full SHA；
5. GUI full SHA；
6. vector MSP；
7. Reset Handler Thumb bit；
8. Reset Handler 落在：
   `0x90000000 .. 0x90000000 + app_size`。

结果只描述 Runtime 是否符合 Record。

---

## 14. Trusted Request 生命周期

Application 使用：

```text
update_request_store.load
```

成功加载后进入 Prepare。

Update Service 不负责 clear request。

成功顺序：

```text
Update Install success
-> Application BootControl Commit
-> Application request_store.clear
-> unmount
-> system_reset
```

clear 失败：

- 记录日志；
- 不回退；
- 仍 reset；
- 下次 stale request 处理。

---

## 15. Stale Request 处理

Application：

```text
request loaded
-> Prepare Manifest
-> compare with Active Record package identity
```

若一致：

```text
ActiveValidation
    success -> clear request -> normal launch/reset path
    failure -> Install same package again
```

这样处理 commit 后清理前掉电。

---

## 16. Recovery

当前不需要旧的 `recovery_candidate_load_fn` 和 pair scanner。

当前 Recovery 语义：

```text
Runtime invalid
    +
Trusted Request + valid package available
        -> reuse Update Service Install
```

若：

```text
Runtime invalid
AND no usable authorized package
```

则：

```text
FAULT
```

可以保留 `recovery_service` 名称作为未来扩展，但当前不得实现 QSPI pair 扫描。

更简单的 V1 实现允许 Application 在 `RECOVERY` 状态直接复用 Update Service。

---

## 17. Launch Service

固定：

```c
#define BOOT_APPLICATION_VECTOR_BASE 0x90000000UL
```

Launch Service 不接受任意跳转地址。

步骤：

1. 确保文件均关闭；
2. 卸载 Package Source；
3. 结束所有 QSPI indirect 操作；
4. 配置 4-byte addressing；
5. 进入 Memory-Mapped；
6. MPU：APP region executable，其他数据 region XN；
7. 清理/同步 Cache；
8. 禁用中断；
9. 停止 SysTick；
10. 清 pending IRQ；
11. 读取 MSP/Reset；
12. 设置 VTOR；
13. `__set_MSP()`；
14. DSB/ISB；
15. 跳转；
16. 返回视为 fault。

---

## 18. Interface 清单

当前核心：

```text
package_source_t
update_request_store_t
async_block_device_t
boot_control_store_t
hash_provider_t
xip_controller_t
application_jump_t
system_reset_t
clock_t
watchdog_t
logger_t
```

可保留 `checksum_t` 用于 EEPROM record CRC 或其他诊断，但固件内容完整性基线是 SHA-256。

---

## 19. Composition

当前开发：

```text
package_source_t       <- SD/FatFs Package Adapter
update_request_store_t <- SD/FatFs Request Adapter
```

正式：

```text
package_source_t       <- eMMC filesystem Package Adapter
update_request_store_t <- eMMC filesystem Request Adapter
```

其他：

```text
async_block_device_t   <- W25Q256 Adapter
boot_control_store_t   <- AT24 Adapter
xip_controller_t       <- STM32H7 QSPI Adapter
application_jump_t     <- Cortex-M Jump Adapter
system_reset_t         <- STM32 Reset Adapter
```

Composition 不包含升级业务。

---

## 20. 静态内存预算

建议：

| 对象 | 预算 |
|---|---:|
| Manifest raw buffer | 16 KiB |
| Manifest JSON tokens | ≤ 4 KiB |
| APP/GUI I/O buffer | 4 KiB, 32-byte aligned |
| SHA context | provider-defined fixed size |
| Active Record read/write | 2 × 256 B |
| Request parsed object | fixed small struct |

删除：

```text
Relocation raw buffer
Parsed relocation entries
APPX header buffer
```

大 buffer 不放函数栈。

---

## 21. 错误模型

稳定错误示例：

```text
UPDATE_ERROR_REQUEST
UPDATE_ERROR_MANIFEST_BINDING
UPDATE_ERROR_MANIFEST_FORMAT
UPDATE_ERROR_SOURCE_APP_HASH
UPDATE_ERROR_SOURCE_GUI_HASH
UPDATE_ERROR_APP_ERASE
UPDATE_ERROR_APP_PROGRAM
UPDATE_ERROR_APP_VERIFY
UPDATE_ERROR_GUI_ERASE
UPDATE_ERROR_GUI_PROGRAM
UPDATE_ERROR_GUI_VERIFY
BOOT_CONTROL_ERROR_COMMIT
ACTIVE_VALIDATION_ERROR_HASH
LAUNCH_ERROR_VECTOR
```

原生 FatFs/HAL/Driver code 只进入 `native_error`。

---

## 22. Build/CMake

Application linker 必须固定：

```text
APP 1 MiB @ 0x90000000
GUI 8 MiB @ 0x90200000
```

Post-build 必须生成：

```text
hmi.app.bin
hmi.gui.bin
manifest.json
```

不得构建：

```text
hmi.app.reloc.bin
*.appx
```

如果已有 `build_appx.py`：

- 删除或归档；
- 不允许继续成为 Release Pipeline 的依赖。

---

## 23. Host 测试

至少：

- Request Schema；
- Request/Manifest SHA binding；
- package_id mismatch；
- Manifest duplicate/unknown/missing keys；
- APP/GUI size boundary；
- APP source SHA mismatch；
- GUI source SHA mismatch；
- target readback SHA mismatch；
- Record A/B sequence wrap；
- Commit Marker 每一步掉电注入；
- stale request + valid Runtime；
- stale request + corrupted Runtime；
- failure before erase keeps old Runtime；
- failure after erase never blindly launches old Record；
- Update Service 每次 `process()` 有界。

---

## 24. Target 测试

至少：

- W25Q256 JEDEC/容量；
- QSPI `FlashSize` 参数实测；
- APP 固定 `0x90000000` XIP；
- GUI 固定 `0x90200000` 读取；
- APP 1 MiB 边界；
- GUI 8 MiB 边界；
- SD 缺失/移除（开发构建）；
- eMMC I/O 错误（正式 Adapter Fake/目标）；
- APP erase/program 随机掉电；
- GUI erase/program 随机掉电；
- Active Record commit 随机掉电；
- commit 后 request clear 前掉电；
- request clear 失败；
- Cache/MPU/QSPI indirect ↔ memory-mapped 切换；
- IWDG 复位后 request 保留并可重试；
- Runtime 无效且无恢复源时不得 Launch。

---

## 25. Codex 禁止项

Codex 不得重新生成：

```text
APP1/APP2
GUI1/GUI2
active_pair
inactive_slot
slot switching
hmi.app.reloc.bin
APPX
relocation_entry_t
ABS32_ADD_XIP_BASE
R_ARM relocation parser
target_crc32.app1/app2
pair recovery scanner
manifest target address
arbitrary jump address
```
