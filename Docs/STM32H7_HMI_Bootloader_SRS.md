# STM32H7 HMI Bootloader 软件需求规格说明书（SRS）

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 文档名称 | STM32H7 HMI Bootloader 软件需求规格说明书 |
| 文档类型 | Software Requirements Specification（SRS） |
| 目标平台 | STM32H743 + W25Q256 QSPI NOR Flash + SD/FatFs + 可选 EEPROM |
| 产品 | HMI |
| 软件形态 | 裸机 Bootloader |
| 升级介质 | SD 卡 |
| 上游升级来源 | HMI.APP 从 U 盘获取升级包并完成认证 |
| 文档版本 | 1.0 |
| 状态 | Baseline |

---

## 2. 目的

本文档定义 HMI Bootloader 的软件需求、升级包格式、存储布局、启动流程、升级流程、失败恢复、快速升级检测、版本策略以及异常处理规则。

本 Bootloader 的核心职责为：

1. 快速判断是否存在由 HMI.APP 提交的升级请求；
2. 校验升级请求与 Manifest 的绑定关系；
3. 校验升级包结构、目标信息、版本条件及文件属性；
4. 将升级文件写入 HMI Runtime 和外部 Therapy MCU；
5. 在写入过程中使用 SHA-256 与逐块回读 `memcmp()` 检查数据正确性；
6. 在升级失败且 Runtime 已被修改时，从 `CURRENT` 恢复上一已确认固件；
7. 在成功升级后，将本次升级包提交为新的 `CURRENT`；
8. 启动当前 HMI.APP；
9. 在不可恢复故障时进入错误状态并依赖看门狗复位。

---

## 3. 系统边界

### 3.1 HMI.APP 职责

HMI.APP 负责升级包进入设备后的可信性验证，包括：

- 从 U 盘识别升级包；
- 校验升级包结构；
- 校验 Manifest；
- 校验各固件文件 SHA-256；
- 执行 ECDSA 签名验证；
- 判断升级包来源是否合法；
- 将完整升级包复制至 SD 卡 `/UPDATE/firmware/`；
- 生成 `/UPDATE/boot_update_request.json`；
- 在启用 EEPROM Fast Update Check 时，将升级请求及 `manifest_sha256` 写入 EEPROM；
- 完成提交后触发系统复位。

### 3.2 HMI.BOOTLOADER 职责

Bootloader 不承担固件发行方认证，不执行 ECDSA 验签。

Bootloader 负责：

- 识别 APP 提交的升级请求；
- 读取并解析 Request 与 Manifest；
- 校验 Request、EEPROM 与 Manifest 的绑定关系；
- 执行 Version Policy；
- 校验文件实际大小；
- 在固件读写过程中计算 SHA-256；
- 写入 QSPI Runtime；
- 通过 STM32 ROM Bootloader 升级 Therapy MCU；
- 对每个写入块执行回读并 `memcmp()`；
- 处理升级失败；
- 从 `CURRENT` 执行恢复；
- 生成并提交新的 `CURRENT`；
- 启动 HMI.APP。

### 3.3 信任模型

系统采用以下信任链：

```text
发行升级包
   ↓
ECDSA 签名
   ↓
HMI.APP 验签
   ↓
APP 验证 Manifest / 固件 SHA-256
   ↓
复制 UPDATE/firmware
   ↓
APP 生成 Request
   ↓
APP 将 manifest_sha256 写入 EEPROM
   ↓
Bootloader 校验 EEPROM / Request / Manifest 三方绑定
   ↓
Bootloader 安装固件
```

Bootloader 不负责重新执行 ECDSA 验签。

---

## 4. 设计原则

Bootloader 应遵循以下原则：

1. Bootloader 逻辑尽量简单；
2. EEPROM 仅用于快速升级检测，不作为 Runtime 状态数据库；
3. SD 卡 Request 是升级请求文件；
4. `CURRENT` 是最近一次已确认、可用于恢复的完整固件包；
5. `CURRENT_NEW` 用于新 `CURRENT` 的事务式构建；
6. 升级前错误不得破坏当前 Runtime；
7. Runtime 一旦进入擦除或写入阶段，失败后不得直接跳转 APP；
8. 所有固件内容校验统一使用 SHA-256；
9. 写入正确性使用逐块回读 + `memcmp()`；
10. 不采用 CRC32 作为固件内容校验机制；
11. 不实现 APP A/B 双运行槽；
12. 不实现 Trial Boot；
13. 不实现 Boot Confirm；
14. 不实现网络 OTA；
15. 不实现 Bootloader 自更新；
16. 不保存多版本历史；
17. 不实现多版本自动回滚；
18. 不使用 EEPROM Active Record A/B 机制。

---

# 5. 软件架构需求

## 5.1 推荐分层

```text
Application
   │
   ├── Boot Manager
   ├── Update Manager
   └── Recovery Manager
          │
          ▼
Services
   ├── Update Request
   ├── Manifest
   ├── Version Policy
   ├── Image Installer
   ├── Current Manager
   ├── App Launcher
   └── Secondary MCU Updater
          │
          ▼
Platform
   ├── File System
   ├── SHA-256
   ├── QSPI Runtime Access
   ├── Watchdog
   ├── Reset
   └── Time
          │
          ▼
BSP / Drivers
   ├── SDMMC
   ├── QSPI
   ├── UART
   ├── EEPROM
   ├── STM32 ROM Boot
   └── HAL / LL / FatFs
```

## 5.2 架构约束

### ARCH-001
Application 层应只负责编排启动、升级、恢复和启动 APP 的业务流程。

### ARCH-002
Manifest、Request、Version Policy、Image Installer 和 Current 管理应与具体 HAL API 解耦。

### ARCH-003
BSP/Driver 不得识别“升级包”“Manifest”“CURRENT”等业务概念。

### ARCH-004
Bootloader 不应引入不必要的 Interfaces / Adapters / Composition 层，除非后续测试或平台复用存在明确需求。

---

# 6. SD 卡目录结构

## 6.1 正常目录

```text
/
├── UPDATE/
│   ├── boot_update_request.json
│   └── firmware/
│       ├── manifest.json
│       ├── hmi.app.bin
│       ├── hmi.gui.bin
│       └── therapy.app.bin
│
├── CURRENT/
│   └── firmware/
│       ├── manifest.json
│       ├── hmi.app.bin
│       ├── hmi.gui.bin
│       └── therapy.app.bin
│
└── CURRENT_NEW/
    └── firmware/
        ├── manifest.json
        ├── hmi.app.bin
        ├── hmi.gui.bin
        └── therapy.app.bin
```

### FS-001
`UPDATE/firmware` 表示 HMI.APP 已认证并提交的待安装固件包。

### FS-002
`CURRENT/firmware` 表示最近一次已经确认、可用于恢复的完整固件包。

### FS-003
`CURRENT_NEW/firmware` 仅用于生成下一版本 `CURRENT`。

### FS-004
正常稳定状态下 `CURRENT_NEW` 不应存在。

### FS-005
Bootloader 不得直接覆盖 `CURRENT/firmware` 中的现有内容。

---

# 7. boot_update_request.json

## 7.1 格式

```json
{
  "format_version": 1,
  "requested": true,
  "package_id": "hmi-1.3.4+20260812",
  "manifest_sha256": "75abc54406863ca345cef7d6342b36e8ef490df80fb72a21239e677ea3448825",
  "component_mask": 7
}
```

若后续产品确认所有组件始终固定升级，可评估删除 `component_mask`；在本版本需求中允许保留。

## 7.2 Request 需求

### REQ-001
Request 文件固定路径应为：

```text
/UPDATE/boot_update_request.json
```

### REQ-002
`format_version` 必须为支持的格式版本。

### REQ-003
`requested` 必须为 `true`。

### REQ-004
`package_id` 必须与 Manifest 中的 `package_id` 一致。

### REQ-005
`manifest_sha256` 必须为 64 个小写十六进制字符。

### REQ-006
Bootloader 应对 `/UPDATE/firmware/manifest.json` 原始文件字节计算 SHA-256。

### REQ-007
实际 Manifest SHA-256 必须与 Request 中的 `manifest_sha256` 一致。

### REQ-008
启用 EEPROM Fast Update Check 时，实际 Manifest SHA-256 还必须与 EEPROM 中保存的 `manifest_sha256` 一致。

---

# 8. manifest.json

## 8.1 格式

```json
{
  "format_version": 1,
  "package_id": "hmi-1.3.4+20260812",
  "release": {
    "major": 1,
    "minor": 3,
    "patch": 4,
    "build": 20260812
  },
  "target": {
    "product": "HMI",
    "hardware": "STM32H743-W25Q256",
    "minimum_bootloader_version": "1.0.0"
  },
  "components": {
    "app": {
      "file": "hmi.app.bin",
      "format": "raw-bin-v1",
      "size": 221960,
      "sha256": "0615b239121650469f23ba153e37550e143a97364a90123f5a4e922588cdd19a"
    },
    "gui": {
      "file": "hmi.gui.bin",
      "format": "raw-bin-v1",
      "size": 7728028,
      "sha256": "d7efdf12b490448b8a78ae3ba669ce5d3ede9710f7ce0c0c4d49b9f982693a30"
    },
    "therapy": {
      "file": "therapy.app.bin",
      "format": "raw-bin-v1",
      "size": 313160,
      "sha256": "8c61700ecd4dcb8ea05cea35e8f8ea71418f23d3e4d7da8099542215b9cf33c7"
    }
  }
}
```

## 8.2 Manifest 校验

### MAN-001
`format_version` 必须等于 Bootloader 支持的版本。

### MAN-002
`target.product` 必须等于 `HMI`。

### MAN-003
`target.hardware` 必须等于当前硬件型号。

### MAN-004
当前 Bootloader 版本必须满足 `minimum_bootloader_version`。

### MAN-005
组件文件名必须使用固定名称。

### MAN-006
APP 文件名固定为：

```text
hmi.app.bin
```

### MAN-007
GUI 文件名固定为：

```text
hmi.gui.bin
```

### MAN-008
Therapy 文件名固定为：

```text
therapy.app.bin
```

### MAN-009
固件格式必须为：

```text
raw-bin-v1
```

### MAN-010
各固件实际文件大小必须等于 Manifest 中声明的 `size`。

### MAN-011
Manifest 中每个 `sha256` 必须是合法 SHA-256 Hex 字符串。

### MAN-012
各组件大小不得超过目标 Runtime 分区或目标 MCU 可写范围。

---

# 9. EEPROM Fast Update Check

## 9.1 功能定位

EEPROM 只用于加速“是否需要初始化 SD/FatFs 并检查升级请求”的判断。

EEPROM 不承担：

- 当前 Runtime 版本存储；
- 历史版本存储；
- A/B 状态；
- Trial/Confirm；
- 回滚记录；
- Runtime 完整性数据库。

## 9.2 BootControl 数据结构

```c
typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t request;
    uint8_t  manifest_sha256[32];
    uint32_t check;
} BootControl_t;
```

推荐定义：

```c
#define BOOT_MAGIC               0x4254434CU
#define BOOT_REQUEST_NONE        0x00000000U
#define BOOT_REQUEST_UPDATE      0x55504454U
```

`check` 用于 BootControl 自身完整性校验。实现可使用 CRC32 或等价的轻量完整性校验算法；该校验仅针对 BootControl 元数据，不用于固件内容校验。

## 9.3 编译配置

```c
#define BOOT_FAST_UPDATE_CHECK_ENABLE  1
```

### EEPROM-001
当 `BOOT_FAST_UPDATE_CHECK_ENABLE == 1` 时，Bootloader 应首先读取 EEPROM BootControl。

### EEPROM-002
当 `BOOT_FAST_UPDATE_CHECK_ENABLE == 0` 时，Bootloader 每次启动均应初始化 SD/FatFs 并检查 Request。

---

# 10. EEPROM 状态处理规则

## 10.1 EEPROM = UPDATE

### EEPROM-010
当 EEPROM 有效且 `request == BOOT_REQUEST_UPDATE` 时，应初始化 SD/FatFs 并检查 Request。

### EEPROM-011
若 Request 存在且合法，应继续执行升级预检查。

### EEPROM-012
若 Request 不存在，应清除 EEPROM 为 `NONE`，然后快速启动 APP。

### EEPROM-013
若 Request 异常，应清除 EEPROM 为 `NONE`，然后快速启动 APP。

### EEPROM-014
若 Manifest 不存在、非法或与 Request 不匹配，应清除 EEPROM 为 `NONE`，然后快速启动 APP。

### EEPROM-015
若实际 Manifest SHA-256 与 EEPROM 中的 `manifest_sha256` 不一致，应清除 EEPROM 为 `NONE`，然后快速启动 APP。

### EEPROM-016
上述 EEPROM 清除并启动 APP 的规则仅适用于 Runtime 尚未开始擦除或写入的阶段。

---

## 10.2 EEPROM = NONE

### EEPROM-020
当 EEPROM 有效且 `request == BOOT_REQUEST_NONE` 时，应直接进入快速启动 APP 流程。

### EEPROM-021
EEPROM 为 NONE 时，不应读取 `/UPDATE/boot_update_request.json`。

### EEPROM-022
EEPROM 为 NONE 时，SD 上 Request 是否存在不影响启动流程。

---

## 10.3 EEPROM 内容异常

### EEPROM-030
若 EEPROM `magic` 非法，应将 EEPROM 重置为 NONE，并快速启动 APP。

### EEPROM-031
若 EEPROM `check` 校验失败，应将 EEPROM 重置为 NONE，并快速启动 APP。

### EEPROM-032
若 EEPROM `request` 为未定义值，应将 EEPROM 重置为 NONE，并快速启动 APP。

### EEPROM-033
EEPROM 异常不得触发 SD Slow Path。

---

# 11. Fast Update Check 关闭时的行为

当 `BOOT_FAST_UPDATE_CHECK_ENABLE == 0` 时：

```text
Boot
 ↓
初始化 SD
 ↓
Mount FatFs
 ↓
检查 Request
 ↓
Request 不存在
    → Boot APP
Request 存在
    → Request / Manifest / Version Policy
    → Upgrade
```

### FAST-001
关闭 EEPROM Fast Update Check 后，EEPROM 不参与升级触发判断。

### FAST-002
关闭 EEPROM Fast Update Check 后，Request 文件成为唯一升级触发条件。

---

# 12. 启动流程

## 12.1 EEPROM Fast Check 开启

```text
RESET
  ↓
Minimal Hardware Init
  ↓
Read EEPROM BootControl
  ↓
Valid UPDATE ?
 ├─ NO
 │   ↓
 │ Fast Boot APP
 │
 └─ YES
     ↓
   SD Init
     ↓
   FatFs Mount
     ↓
   Check Request
     ↓
   Validate Request
     ↓
   Validate Manifest
     ↓
   Manifest Hash Binding
     ↓
   Version Policy
     ↓
   Upgrade
```

## 12.2 快速 APP 启动

快速启动路径不得初始化非必要升级介质。

推荐流程：

```text
Minimal Init
 ↓
QSPI Runtime准备
 ↓
读取APP Vector
 ↓
检查 MSP
 ↓
检查 Reset Handler
 ↓
进入 QSPI Memory-Mapped
 ↓
Cache处理
 ↓
VTOR
 ↓
MSP
 ↓
Jump APP
```

---

# 13. Version Policy

## 13.1 版本字段

版本比较应至少包含：

```text
major
minor
patch
build
```

比较顺序按字典序执行：

```text
major
 → minor
 → patch
 → build
```

## 13.2 升级规则

### VER-001
当 `UPDATE.release >= CURRENT.release` 时，允许执行升级。

### VER-002
当 `UPDATE.release < CURRENT.release` 时，拒绝升级。

### VER-003
相同版本允许重新安装。

### VER-004
Version Policy 仅适用于 UPDATE 安装，不适用于 CURRENT Recovery。

### VER-005
当 `CURRENT` 不存在时，应允许正常执行 UPDATE。

### VER-006
Version Policy 拒绝发生在 Runtime 修改之前，因此拒绝后可取消本次升级并启动当前 APP。

---

# 14. 固件安装通用流程

所有固件写入应使用同一种逻辑模型：

```text
Open Source
 ↓
Check Actual Size
 ↓
SHA256_Init
 ↓
Erase Target
 ↓
Loop:
    Read Source Block
    SHA256_Update(Source)
    Write Target Block
    Read Target Block
    memcmp(Source, Target)
 ↓
SHA256_Final
 ↓
Compare with Manifest SHA256
```

## 14.1 校验要求

### IMG-001
不得使用 CRC32 作为固件文件内容校验算法。

### IMG-002
固件整体完整性应使用 SHA-256。

### IMG-003
固件写入后应逐块读取目标数据。

### IMG-004
每个写入块应使用 `memcmp()` 与源块精确比较。

### IMG-005
完成所有块写入后，应比较计算所得 SHA-256 与 Manifest 中的 SHA-256。

### IMG-006
不要求在全部写入完成后再次完整读取目标区域计算第二遍 SHA-256。

### IMG-007
任何 `memcmp()` 失败均视为当前组件安装失败。

### IMG-008
任何 SHA-256 不匹配均视为当前组件安装失败。

---

# 15. APP Runtime 安装

### APP-001
APP 固件源路径：

```text
/UPDATE/firmware/hmi.app.bin
```

### APP-002
写入前必须检查文件实际大小。

### APP-003
写入前必须确保 QSPI 已退出 memory-mapped/XIP 模式。

### APP-004
必须确认 QSPI 处于可擦写的 indirect 模式。

### APP-005
擦除 APP Runtime 前，应设置 `runtime_modified = true`。

### APP-006
APP 每个写入块应执行回读 `memcmp()`。

### APP-007
APP 完整文件应执行 SHA-256 并与 Manifest 比较。

---

# 16. GUI Runtime 安装

### GUI-001
GUI 固件源路径：

```text
/UPDATE/firmware/hmi.gui.bin
```

### GUI-002
GUI 使用与 APP 相同的 size、SHA-256、write/readback/memcmp 机制。

### GUI-003
GUI 安装失败应视为整个 Package 安装失败。

---

# 17. Therapy MCU 安装

### THERAPY-001
Therapy 固件源路径：

```text
/UPDATE/firmware/therapy.app.bin
```

### THERAPY-002
Bootloader 应通过目标 MCU 的 STM32 System Memory ROM Bootloader 进行升级。

### THERAPY-003
Therapy MCU 写入前应控制目标设备进入 ROM Boot 模式。

### THERAPY-004
每个 Therapy 写入块应执行 ROM `WRITE MEMORY`。

### THERAPY-005
每个 Therapy 写入块应执行 ROM `READ MEMORY`。

### THERAPY-006
读回数据必须与源块执行 `memcmp()`。

### THERAPY-007
Therapy 整个源文件必须执行 SHA-256 并与 Manifest 比较。

### THERAPY-008
Therapy 安装失败应视为整个 Package 安装失败。

---

# 18. Package 原子性

一个 Manifest 中声明的全部组件构成一个整体发布包。

### PKG-001
APP、GUI、Therapy 中任一必要组件失败，都应视为整个 Package 安装失败。

### PKG-002
不得把“部分组件成功”提交为新的 CURRENT。

### PKG-003
Package 成功的定义是：

- 所有目标组件写入完成；
- 所有块级 readback + `memcmp()` 成功；
- 所有源文件 SHA-256 与 Manifest 一致；
- 外部 MCU 升级流程成功结束；
- 新 CURRENT 构建并验证成功。

---

# 19. CURRENT 与 CURRENT_NEW

## 19.1 CURRENT 定义

`CURRENT` 定义为：

> 最近一次已经确认、完整、可用于恢复的固件包。

`CURRENT` 不应简单理解为“当前 Flash 中正在运行的 Runtime”，因为升级事务执行期间 Runtime 与 CURRENT 可能短暂处于不同版本。

## 19.2 CURRENT_NEW 构建

正常升级成功后：

```text
UPDATE/firmware
 ↓
Copy to CURRENT_NEW/firmware
 ↓
Verify File Set
 ↓
Verify Size
 ↓
Verify SHA-256
 ↓
Commit CURRENT_NEW → CURRENT
```

### CURRENT-001
不得在新包尚未验证完成前覆盖旧 CURRENT。

### CURRENT-002
写入 CURRENT_NEW 后，应对其中所有文件执行完整 size 和 SHA-256 校验。

### CURRENT-003
只有 CURRENT_NEW 完整验证通过后，才允许替换 CURRENT。

### CURRENT-004
CURRENT_NEW 校验失败时，旧 CURRENT 必须保持不变。

### CURRENT-005
正常稳定状态下应删除残留 CURRENT_NEW。

---

# 20. CURRENT 切换与掉电恢复

FAT/FatFs 不提供数据库意义上的事务，因此 Bootloader 应处理 CURRENT_NEW 残留。

### CURRENT-010
若启动时发现：

```text
CURRENT 存在
CURRENT_NEW 存在
```

应优先保留有效 CURRENT，并清理或重新校验 CURRENT_NEW。

### CURRENT-011
若：

```text
CURRENT 不存在
CURRENT_NEW 存在
```

Bootloader 可校验 CURRENT_NEW。

### CURRENT-012
若 CURRENT_NEW 完整有效，可恢复为 CURRENT。

### CURRENT-013
若 CURRENT_NEW 无效，应删除 CURRENT_NEW，不得将其作为 Recovery Source。

---

# 21. Request / EEPROM 提交顺序

## 21.1 升级成功

完成整个升级及 CURRENT 提交后：

```text
Upgrade Success
 ↓
CURRENT Commit Success
 ↓
Delete / Invalidate Request
 ↓
EEPROM = NONE
 ↓
Reset
```

### COMMIT-001
Request 必须先于 EEPROM 清除。

### COMMIT-002
只有升级成功且 CURRENT 成功提交后，才允许清除 Request。

### COMMIT-003
只有 Request 清除成功后，才允许将 EEPROM 设置为 NONE。

### COMMIT-004
若 Request 已成功清除但 EEPROM 清除失败，则下次 Bootloader 启动时：

```text
EEPROM = UPDATE
Request = Missing
```

Bootloader 应：

1. 清 EEPROM；
2. 快速启动 APP。

---

# 22. 升级前错误处理

定义：

```text
PRE-INSTALL
```

表示尚未执行任何可能破坏当前 Runtime 的操作。

典型错误：

- Request 不存在；
- Request JSON 错误；
- Manifest 不存在；
- Manifest JSON 错误；
- Manifest SHA 不匹配；
- EEPROM Manifest Hash 不匹配；
- target 不匹配；
- minimum bootloader version 不满足；
- Version Policy 拒绝；
- 文件缺失；
- 文件 size 非法。

### ERR-PRE-001
PRE-INSTALL 错误不得擦除或修改 Runtime。

### ERR-PRE-002
EEPROM Fast Check 开启时，PRE-INSTALL Request/Manifest 异常应将 EEPROM 清为 NONE。

### ERR-PRE-003
完成 EEPROM 清理后，应快速启动当前 APP。

---

# 23. Runtime 修改标志

Bootloader 应维护：

```c
bool runtime_modified;
```

### RUNTIME-001
系统进入升级流程时：

```c
runtime_modified = false;
```

### RUNTIME-002
在任何目标 Runtime 第一次擦除或破坏性写入之前，应设置：

```c
runtime_modified = true;
```

### RUNTIME-003
一旦 `runtime_modified == true`，任何后续升级失败都不得直接跳转当前 APP。

### RUNTIME-004
`runtime_modified == true` 的失败必须进入 CURRENT Recovery。

---

# 24. 安装重试

### RETRY-001
单个升级事务安装失败后，可执行最多 2 次重试。

### RETRY-002
重试前应重新初始化对应目标写入状态。

### RETRY-003
连续重试仍失败，且 `runtime_modified == false` 时，可取消升级并启动当前 APP。

### RETRY-004
连续重试仍失败，且 `runtime_modified == true` 时，必须进入 Recovery。

---

# 25. CURRENT Recovery

## 25.1 Recovery 触发

当 Runtime 已被修改且 UPDATE 安装失败时：

```text
Install Failure
 ↓
CURRENT exists?
 ├─ YES → Recovery
 └─ NO  → Fatal Error
```

### REC-001
Recovery 应从：

```text
/CURRENT/firmware/
```

读取上一已确认固件包。

### REC-002
Recovery 不执行 Version Policy。

### REC-003
Recovery 使用与正常安装相同的：

- size 检查；
- SHA-256；
- write；
- readback；
- `memcmp()`。

### REC-004
Recovery 应恢复 Manifest 中声明的整个 Package，而不是只恢复失败组件。

### REC-005
若 APP、GUI、Therapy 作为一个 Package，则 Recovery 应将其恢复到 CURRENT 对应的同一版本集合。

---

# 26. CURRENT 不存在

### REC-010
若首次升级前 CURRENT 不存在，则系统不具备旧版本 Recovery Source。

### REC-011
若 `runtime_modified == true` 且 CURRENT 不存在，应停止启动当前 APP。

### REC-012
系统应持续输出明确错误日志。

### REC-013
系统可进入错误循环并等待 IWDG 复位。

### REC-014
此时不得清除 Request。

### REC-015
此时不得清除 EEPROM UPDATE。

### REC-016
下一次复位后应再次尝试升级。

---

# 27. Recovery 成功

### REC-020
Recovery 全部成功后，应清除或失效 UPDATE Request。

### REC-021
Request 清除成功后，应将 EEPROM 设置为 NONE。

### REC-022
Recovery 成功后应执行系统复位。

---

# 28. Recovery 失败

### REC-030
Recovery 每次失败可重试最多 2 次。

### REC-031
Recovery 最终失败后不得启动 APP。

### REC-032
Recovery 失败后应保留 Request。

### REC-033
Recovery 失败后应保留 EEPROM UPDATE。

### REC-034
系统应持续输出错误日志。

### REC-035
系统应进入看门狗复位或 Fatal Error Loop。

---

# 29. APP 启动需求

### LAUNCH-001
启动 APP 前应确保 QSPI Runtime 处于正确工作模式。

### LAUNCH-002
应读取 APP Vector Table。

### LAUNCH-003
应检查 MSP 是否位于允许的 SRAM 地址范围。

### LAUNCH-004
应检查 Reset Handler 是否位于合法 APP 地址范围。

### LAUNCH-005
应检查 Reset Handler Thumb 位。

### LAUNCH-006
应进入 QSPI Memory-Mapped 模式。

### LAUNCH-007
应执行必要的 Cache invalidate / barrier。

### LAUNCH-008
应设置 VTOR。

### LAUNCH-009
跳转前应清理 SysTick/NVIC 等 Bootloader 残留状态。

### LAUNCH-010
应设置 MSP。

### LAUNCH-011
应跳转到 APP Reset Handler。

### LAUNCH-012
若 Jump 前检查失败，应进入 Fatal Error。

---

# 30. 看门狗和故障行为

### WDG-001
正常 Bootloader 流程应按策略刷新 IWDG。

### WDG-002
不可恢复错误发生后应停止正常流程。

### WDG-003
Fatal Error 状态可持续输出日志并停止喂狗。

### WDG-004
IWDG 复位后系统重新从 Bootloader 开始执行。

### WDG-005
升级或恢复失败不得通过直接 Jump APP 规避错误。

---

# 31. 日志需求

### LOG-001
Bootloader 至少支持：

- ERROR；
- WARN；
- INFO。

### LOG-002
以下事件必须记录 INFO：

- Bootloader 启动；
- Fast Boot；
- Update Request detected；
- Manifest validated；
- Version accepted；
- APP install start/success；
- GUI install start/success；
- Therapy install start/success；
- CURRENT_NEW build；
- CURRENT commit；
- Recovery start/success；
- APP launch。

### LOG-003
以下事件必须记录 ERROR：

- EEPROM BootControl invalid；
- Request invalid；
- Manifest invalid；
- Manifest hash mismatch；
- Version rejected；
- file size mismatch；
- file SHA mismatch；
- Flash erase/write/read failure；
- block `memcmp()` failure；
- Therapy ROM Boot failure；
- CURRENT_NEW verify failure；
- Recovery failure；
- APP vector invalid；
- APP jump failure。

---

# 32. SD/FatFs 要求

### FAT-001
SD 初始化失败时，不得尝试访问 FatFs。

### FAT-002
FatFs Mount 失败时，应根据当前阶段决定：

- PRE-INSTALL：取消升级并启动当前 APP；
- Runtime 已修改：进入 Recovery/Fatal 路径。

### FAT-003
对重要文件写入完成后应调用 `f_sync()` 或等效操作。

### FAT-004
CURRENT_NEW 完整写入和校验后才能进入目录切换。

### FAT-005
Request 删除成功后再允许清 EEPROM。

---

# 33. 文件集合要求

### FILE-001
`UPDATE/firmware` 应仅包含当前产品允许的固定文件集合。

### FILE-002
Bootloader 应识别：

```text
manifest.json
hmi.app.bin
hmi.gui.bin
therapy.app.bin
```

### FILE-003
文件名比较应区分大小写。

### FILE-004
缺失 Manifest 声明文件应拒绝升级。

### FILE-005
Manifest 声明的文件必须与实际固定文件名一致。

---

# 34. 性能需求

### PERF-001
启用 EEPROM Fast Update Check 后，正常无升级启动路径不得初始化 SD/FatFs。

### PERF-002
Bootloader 不应为了验证大型固件在安装前额外完整读取一遍文件。

### PERF-003
固件 SHA-256 应在正常读取并写入过程中流式计算。

### PERF-004
不要求目标 Runtime 完成后再完整读取并执行第二遍 SHA-256。

### PERF-005
每个写入块仍必须执行 readback + `memcmp()`。

---

# 35. 安全需求

### SEC-001
升级包发行方认证由 HMI.APP 的 ECDSA 验签负责。

### SEC-002
Bootloader 不重复执行 ECDSA 验签。

### SEC-003
启用 EEPROM Fast Update Check 时，EEPROM 必须保存 APP 已验证 Manifest 的 SHA-256。

### SEC-004
Bootloader 必须验证：

```text
EEPROM.manifest_sha256
==
Request.manifest_sha256
==
SHA256(actual manifest.json)
```

### SEC-005
上述三者不一致时不得执行升级。

### SEC-006
Bootloader 不提供防物理攻击安全根。

### SEC-007
Bootloader 不实现 Secure Boot Chain。

### SEC-008
Bootloader 不实现安全防回滚计数器。

---

# 36. 可配置项

建议统一定义：

```c
#define BOOT_FAST_UPDATE_CHECK_ENABLE     1
#define BOOT_INSTALL_RETRY_COUNT          2
#define BOOT_RECOVERY_RETRY_COUNT         2
```

可进一步配置：

```c
#define BOOT_PRODUCT_NAME                 "HMI"
#define BOOT_HARDWARE_NAME                "STM32H743-W25Q256"
#define BOOT_APP_FILE_NAME                "hmi.app.bin"
#define BOOT_GUI_FILE_NAME                "hmi.gui.bin"
#define BOOT_THERAPY_FILE_NAME            "therapy.app.bin"
```

---

# 37. 推荐顶层状态机

```text
BOOT_START
   ↓
FAST_CHECK
   │
   ├── NO UPDATE
   │      ↓
   │   APP_LAUNCH
   │
   └── UPDATE
          ↓
       SD_MOUNT
          ↓
       REQUEST_CHECK
          ↓
       MANIFEST_CHECK
          ↓
       VERSION_POLICY
          ↓
       INSTALL
        /     \
    SUCCESS   FAILURE
       |          |
       |      runtime_modified?
       |       /          \
       |     NO            YES
       |     |              |
       |  CANCEL          RECOVERY
       |                    |
       ↓                    |
 CURRENT_NEW                |
       ↓                    |
 CURRENT_VERIFY             |
       ↓                    |
 CURRENT_COMMIT             |
       ↓                    |
 CLEAR_REQUEST <------------+
       ↓
 EEPROM_NONE
       ↓
 RESET
```

---

# 38. 推荐模块划分

```text
Application/
├── boot_manager.c
├── update_manager.c
└── recovery_manager.c

Services/
├── update_request.c
├── manifest.c
├── version_policy.c
├── image_installer.c
├── current_manager.c
├── app_launcher.c
└── secondary_mcu_updater.c

Platform/
├── platform_fs.c
├── platform_sha256.c
├── platform_qspi.c
├── platform_watchdog.c
├── platform_reset.c
└── platform_time.c

BSP/
├── bsp_sd.c
├── bsp_qspi.c
├── bsp_eeprom.c
├── bsp_uart.c
└── bsp_secondary_mcu.c

Drivers/
├── spi_nor/
└── stm32_isp/

Middleware/
└── FatFs/

Core/
└── main.c
```

---

# 39. 不在本版本范围内

以下功能明确不属于本版本需求：

- APP A/B 双运行槽；
- GUI A/B 双运行槽；
- Trial Boot；
- Application Confirm；
- 自动运行槽回滚；
- 多版本历史选择；
- 网络 OTA；
- Bootloader 自升级；
- Bootloader 内 ECDSA 验签；
- 公钥轮换；
- 安全计数器；
- TrustZone 安全根；
- 加密固件包；
- Bootloader 内完整发行密钥生命周期管理；
- EEPROM Active Record A/B；
- Runtime 启动前全量 SHA-256；
- 固件 CRC32 内容校验。

---

# 40. 验收标准

## 40.1 正常启动

### AC-001
EEPROM Fast Check 开启、EEPROM=NONE 时，Bootloader 不初始化 SD，并成功启动 APP。

### AC-002
EEPROM Fast Check 关闭时，每次启动均检查 SD Request。

---

## 40.2 正常升级

### AC-010
APP 提交合法 Request 后，Bootloader 能识别升级请求。

### AC-011
Request、EEPROM、Manifest SHA-256 一致时允许进入升级。

### AC-012
APP、GUI、Therapy 全部升级成功。

### AC-013
所有组件均通过 SHA-256。

### AC-014
所有写入块均通过 readback + `memcmp()`。

### AC-015
成功升级后生成合法 CURRENT_NEW。

### AC-016
CURRENT_NEW 验证成功后切换为 CURRENT。

### AC-017
成功提交 CURRENT 后删除 Request。

### AC-018
成功删除 Request 后清 EEPROM。

### AC-019
复位后可正常启动新 APP。

---

## 40.3 Request/Manifest 异常

### AC-020
EEPROM=UPDATE 但 Request 不存在时，应清 EEPROM 并启动 APP。

### AC-021
Request 非法时，应清 EEPROM 并启动 APP。

### AC-022
Manifest 非法时，应清 EEPROM 并启动 APP。

### AC-023
Manifest Hash 不匹配时，应清 EEPROM 并启动 APP。

---

## 40.4 安装失败

### AC-030
Flash 写入块 readback 不一致时，应判定安装失败。

### AC-031
固件最终 SHA-256 不匹配时，应判定安装失败。

### AC-032
Runtime 未修改前失败，可取消升级并启动旧 APP。

### AC-033
Runtime 已修改后失败，不得启动 APP。

### AC-034
Runtime 已修改且 CURRENT 存在时，应执行 Recovery。

---

## 40.5 Recovery

### AC-040
Recovery 能从 CURRENT 恢复 APP。

### AC-041
Recovery 能从 CURRENT 恢复 GUI。

### AC-042
Recovery 能从 CURRENT 恢复 Therapy。

### AC-043
Recovery 不执行 Version Policy。

### AC-044
Recovery 成功后删除 Request。

### AC-045
Request 删除成功后清 EEPROM。

### AC-046
Recovery 成功后系统复位并启动恢复后的 Runtime。

---

## 40.6 Recovery 不可用

### AC-050
首次升级失败且 CURRENT 不存在时，不得启动损坏的 APP。

### AC-051
应持续输出明确 Fatal/Recovery unavailable 日志。

### AC-052
应保留 Request。

### AC-053
应保留 EEPROM UPDATE。

### AC-054
IWDG 复位后应重新尝试升级。

---

# 41. 关键状态约束汇总

必须始终满足：

```text
1. EEPROM NONE
   → Fast Boot
   → 不关心 SD Request

2. EEPROM UPDATE
   → 才检查 SD Request

3. EEPROM 异常
   → 清 EEPROM
   → Fast Boot

4. Request/Manifest 在 PRE-INSTALL 阶段异常
   → 清 EEPROM
   → Boot APP

5. Runtime 一旦开始擦除/写入
   → 失败不得 Boot APP

6. Runtime 修改后升级失败
   → CURRENT Recovery

7. CURRENT Recovery 成功
   → 删除 Request
   → EEPROM NONE
   → Reset

8. Recovery 失败
   → 保留 Request
   → 保留 EEPROM UPDATE
   → Fatal / Watchdog Reset

9. 新 CURRENT 必须先构建 CURRENT_NEW

10. CURRENT_NEW 校验通过前不得覆盖旧 CURRENT

11. 固件内容校验使用 SHA-256

12. 写入正确性使用 readback + memcmp
```

---

# 42. 最终目标行为

Bootloader 最终应表现为：

```text
正常启动：
EEPROM NONE
→ 不访问 SD
→ 快速启动 APP


正常升级：
APP ECDSA 验签
→ UPDATE
→ Request
→ EEPROM UPDATE + Manifest SHA
→ Reset
→ Bootloader
→ 校验
→ Version Policy
→ 安装
→ CURRENT_NEW
→ CURRENT
→ 删除 Request
→ EEPROM NONE
→ Reset
→ 新 APP


升级失败（未修改 Runtime）：
取消升级
→ EEPROM NONE
→ 旧 APP


升级失败（已修改 Runtime）：
CURRENT Recovery
→ 成功
→ 删除 Request
→ EEPROM NONE
→ Reset
→ 旧 Runtime


升级失败且无 CURRENT：
保留 Request
→ 保留 EEPROM UPDATE
→ 输出错误
→ Watchdog Reset
→ 下次继续尝试
```

---

# 43. 结论

本需求定义的是一套：

> **单 Runtime + SD UPDATE/CURRENT Recovery + 可选 EEPROM Fast Update Check + SHA-256 + Readback/Memcmp**

的 STM32H7 裸机 Bootloader。

该方案不依赖 A/B Runtime，不引入复杂 Active Record，也不在 Bootloader 内重复执行 ECDSA 验签。

其核心设计目标为：

- 正常启动快；
- 升级职责清晰；
- 状态数量少；
- 写入过程可检测；
- 升级失败可恢复；
- 无 Recovery Source 时安全停止；
- 掉电后状态可理解；
- 后续实现和测试成本可控。
