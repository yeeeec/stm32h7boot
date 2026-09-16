# STM32H7 HMI Bootloader 架构设计理念

## 1. 文档目的

本文档定义 STM32H7 HMI Bootloader 的软件架构设计理念、分层原则、模块职责、依赖规则及抽象边界。

本文档不用于描述具体升级流程细节，而用于回答以下问题：

- 为什么采用当前架构；
- 为什么不采用更复杂的 Clean Architecture；
- 哪些内容必须解耦；
- 哪些内容不应继续封装；
- Application、Services、Platform、BSP、Driver 各自负责什么；
- 模块之间允许如何依赖；
- 后续新增功能时应遵守什么原则。

本架构面向：

- STM32H743；
- 裸机 Bootloader；
- SD + FatFs；
- W25Q256 QSPI NOR Flash；
- 可选 EEPROM Fast Update Check；
- 外部 Therapy MCU；
- 固定升级包格式；
- 单 Runtime；
- SD `CURRENT` Recovery。

---

# 2. 核心设计目标

本项目的架构目标不是追求“层数多”或“抽象完整”，而是：

> 在保持 Bootloader 简单、确定、可维护的前提下，把业务逻辑与硬件细节有效解耦。

核心目标包括：

1. Bootloader 业务流程清晰；
2. 上层业务不直接依赖 STM32 HAL；
3. 上层业务不直接依赖 FatFs；
4. Manifest、Version Policy、升级状态机不感知具体硬件；
5. BSP 和 Driver 不理解升级业务；
6. 不为单一实现引入无意义的 Interface / Adapter；
7. 调用链尽量短；
8. 每一层都有明确存在价值；
9. 后续更换 Flash、文件系统或 Hash 实现时，上层改动最小；
10. 后续新增业务功能时，不破坏底层驱动稳定性。

---

# 3. 总体设计思想

推荐架构：

```text
Application
    ↓
Services
    ↓
Platform
    ↓
BSP / Drivers
```

实际工程中进一步细分为：

```text
Application
    │
    ├── Boot Manager
    ├── Update Manager
    └── Recovery Manager
            │
            ▼
Services
    │
    ├── Update Request
    ├── Manifest
    ├── Version Policy
    ├── Image Installer
    ├── Current Manager
    ├── App Launcher
    └── Therapy Updater
            │
            ▼
Platform
    │
    ├── Storage
    ├── Flash
    ├── Hash
    ├── Boot Control
    ├── Watchdog
    └── Reset
            │
            ▼
BSP
    │
    ├── QSPI
    ├── SD
    ├── EEPROM
    ├── UART
    └── Therapy MCU Board Control
            │
            ▼
Drivers / Middleware
    │
    ├── SPI NOR
    ├── AT24
    ├── STM32 ROM Boot
    ├── FatFs
    └── STM32 HAL / LL
```

---

# 4. 架构的核心原则

## 4.1 业务和硬件必须分离

最重要的设计原则：

```text
业务流程
≠
平台能力
≠
板级硬件
≠
芯片驱动
```

例如：

```text
“安装 APP”
```

属于业务。

```text
“写 Runtime Flash”
```

属于平台能力。

```text
“当前板子的 QSPI Flash 是 W25Q256”
```

属于 BSP。

```text
“W25Q256 Page Program 命令”
```

属于 Driver。

因此正确调用关系为：

```text
ImageInstaller
    ↓
PlatformFlash
    ↓
BSP_QSPI
    ↓
SpiNor Driver
    ↓
HAL_QSPI
```

而不是：

```text
ImageInstaller
    ↓
HAL_QSPI
```

---

# 5. 为什么不采用复杂 Clean Architecture

传统 Clean Architecture 可能设计为：

```text
Application
    ↓
Use Case
    ↓
Interface
    ↓
Adapter
    ↓
Platform
    ↓
BSP
    ↓
Driver
```

并进一步定义：

```text
IFlashStorage
IFileStorage
IHashProvider
IResetProvider
IBootControlRepository
```

对于大型软件、多平台软件或需要大量 Mock 的系统，这种方式有价值。

但当前 Bootloader 特征为：

- MCU 固定；
- 板卡固定；
- 文件系统固定；
- QSPI 固定；
- EEPROM 固定；
- Therapy MCU 升级接口固定；
- 无 RTOS；
- 生命周期简单；
- 功能边界明确。

如果继续增加：

```text
Interface
Adapter
Repository
Provider
Gateway
Facade
Composition Root
```

会导致：

```text
ImageInstaller
 ↓
IFlashStorage
 ↓
FlashStorageAdapter
 ↓
PlatformFlash
 ↓
BSP_QSPI
 ↓
SpiNor
 ↓
HAL
```

调用链过长，却没有增加真正的业务价值。

因此本项目不采用“为了抽象而抽象”的设计。

---

# 6. 本项目的抽象原则

只在以下场景建立明确抽象边界：

## 6.1 业务与硬件之间

例如：

```text
ImageInstaller
    ↓
PlatformFlash
```

必须抽象。

原因：

- ImageInstaller 不应知道 QSPI；
- ImageInstaller 不应知道 HAL；
- ImageInstaller 只需要“擦、写、读”。

---

## 6.2 业务与中间件之间

例如：

```text
CurrentManager
    ↓
PlatformStorage
    ↓
FatFs
```

必须抽象。

原因：

- CurrentManager 应理解目录和文件语义；
- 但不应操作 `FIL`、`FRESULT` 等 FatFs 类型。

---

## 6.3 可替换的软件能力

例如：

```text
PlatformHash
```

有价值。

因为 SHA-256 后续可能来自：

- 软件实现；
- mbedTLS；
- STM32 Hash 外设；
- 其他 Crypto Library。

上层不需要知道具体实现。

---

# 7. 不应该抽象的场景

## 7.1 纯业务模块不再套接口

例如：

```text
VersionPolicy
```

本身已经是业务算法。

不应设计：

```text
IVersionPolicy
VersionPolicyImpl
VersionPolicyAdapter
```

直接：

```c
VersionPolicy_Check(...);
```

即可。

---

## 7.2 Manifest 不再套 Provider

Manifest 本身是纯数据和规则。

不应设计：

```text
IManifestProvider
ManifestProviderImpl
ManifestAdapter
```

直接：

```c
Manifest_Parse();
Manifest_Validate();
```

即可。

---

## 7.3 UpdateManager 不需要 Controller

不应出现：

```text
UpdateController
    ↓
UpdateManager
    ↓
UpdateService
```

一个顶层业务模块足够。

---

# 8. Application 层设计理念

Application 层负责：

> 决定“什么时候做什么”。

Application 不负责：

> 具体“怎么做”。

推荐模块：

```text
boot_manager
update_manager
recovery_manager
```

---

## 8.1 Boot Manager

Boot Manager 是 Bootloader 最高层入口。

负责：

- 判断是否需要检查升级；
- 决定是否进入 Update；
- 决定是否进入 Recovery；
- 决定是否启动 APP；
- 处理最终错误流程。

Boot Manager 不负责：

- JSON；
- SHA-256；
- FatFs；
- QSPI；
- EEPROM I2C；
- STM32 ROM Boot 协议。

---

## 8.2 Update Manager

Update Manager 负责：

- Request 检查；
- Manifest 检查；
- Version Policy；
- 组件安装顺序；
- CURRENT_NEW 创建；
- CURRENT 提交；
- Request 清除；
- EEPROM 清除。

Update Manager 应描述完整升级业务，但不直接访问 HAL。

---

## 8.3 Recovery Manager

Recovery Manager 独立于 Update Manager。

原因：

正常升级：

```text
UPDATE
→ Version Policy
→ Install
```

Recovery：

```text
CURRENT
→ Direct Restore
```

Recovery 不应经过 Version Policy。

因此不能简单把 Recovery 当成一次普通 Update。

---

# 9. Services 层设计理念

Services 是：

> 与硬件无关的 Bootloader 核心业务能力。

Services 可以理解：

- firmware；
- manifest；
- version；
- UPDATE；
- CURRENT；
- Runtime；
- package。

Services 不应该理解：

- SDMMC1；
- QUADSPI；
- I2C1；
- USART2；
- HAL_StatusTypeDef；
- FatFs FIL。

---

# 10. Update Request 模块

职责：

- 解析 `boot_update_request.json`；
- 校验格式；
- 提供 Request 数据结构；
- 校验 package_id；
- 校验 manifest_sha256。

它不负责：

- 打开文件；
- 读取 SD；
- 计算 SHA；
- EEPROM 操作。

推荐接口：

```c
bool UpdateRequest_Parse(
    const uint8_t *data,
    size_t size,
    UpdateRequest_t *request);
```

---

# 11. Manifest 模块

职责：

- Manifest JSON 解析；
- 字段合法性检查；
- target 检查；
- component 检查；
- file name 检查；
- size 范围检查；
- SHA-256 字符串解析。

不负责：

- 打开 bin；
- 写 Flash；
- 升级 Therapy；
- 修改 CURRENT。

---

# 12. Version Policy 模块

职责：

```text
UPDATE.release
vs
CURRENT.release
```

并输出：

```text
ALLOW
REJECT
```

规则：

```text
UPDATE >= CURRENT
    → ALLOW

UPDATE < CURRENT
    → REJECT
```

CURRENT 不存在：

```text
ALLOW
```

Recovery 不经过 Version Policy。

该模块应保持为纯函数。

---

# 13. Image Installer

Image Installer 是本项目最重要的可复用业务模块之一。

原因：

正常升级和 Recovery 均需要：

```text
打开源文件
检查 size
SHA256_Init
擦除目标
循环读取
SHA256_Update
写目标
读回
memcmp
SHA256_Final
校验
```

因此：

```text
UPDATE
   ↓
ImageInstaller

CURRENT
   ↓
ImageInstaller
```

不应该实现两套写入逻辑。

推荐模型：

```c
typedef struct
{
    const char *source_path;

    uint32_t target_address;
    uint32_t target_max_size;

    uint32_t expected_size;
    uint8_t expected_sha256[32];

} ImageInstallPlan_t;
```

然后：

```c
ImageInstallResult_t
ImageInstaller_Install(
    const ImageInstallPlan_t *plan);
```

---

# 14. Current Manager

Current Manager 负责 SD 上：

```text
UPDATE
CURRENT
CURRENT_NEW
```

之间的文件事务。

它负责：

- 建立 CURRENT_NEW；
- 复制升级包；
- 校验 CURRENT_NEW；
- 提交新的 CURRENT；
- 清理残留 CURRENT_NEW；
- 处理启动时 CURRENT_NEW 异常状态。

Current Manager 不负责：

- QSPI Runtime；
- APP Jump；
- Therapy MCU；
- Version Policy。

---

# 15. App Launcher

App Launcher 负责：

- APP Vector 检查；
- MSP 检查；
- Reset Handler 检查；
- QSPI XIP 准备；
- Cache；
- VTOR；
- NVIC/SysTick 清理；
- MSP 设置；
- APP Jump。

App Launcher 应作为独立模块。

原因：

APP 启动属于一个完整、可独立验证的能力，不应该散布在 Boot Manager 中。

---

# 16. Therapy Updater

Therapy MCU 与本机 QSPI Runtime 安装机制不同。

Therapy Updater 负责：

- Therapy 进入 ROM Boot；
- 擦除；
- WRITE MEMORY；
- READ MEMORY；
- readback `memcmp()`；
- SHA-256；
- Therapy Reset；
- 恢复正常模式。

具体 USART、BOOT0、RESET GPIO 由 BSP / Driver 提供。

---

# 17. Platform 层设计理念

Platform 是整个架构中最关键的解耦层。

它的定义是：

> 向业务层提供稳定的软件能力，但隐藏 HAL、中间件和具体硬件实现细节。

Platform API 应简单、直接、稳定。

---

# 18. Platform Storage

负责：

- Open；
- Read；
- Write；
- Close；
- File Size；
- Exists；
- Remove；
- Rename；
- Mkdir；
- Sync。

内部可以基于 FatFs。

Services 不应该出现：

```c
FIL
FRESULT
f_open
f_read
f_write
```

---

# 19. Platform Flash

向业务提供：

```c
PlatformFlash_Erase();
PlatformFlash_Write();
PlatformFlash_Read();
PlatformFlash_EnterXip();
PlatformFlash_ExitXip();
```

内部可以：

```text
PlatformFlash
    ↓
BSP_QSPI
    ↓
SPI NOR Driver
```

上层不关心 W25Q256 命令。

---

# 20. Platform Hash

推荐：

```c
PlatformHash_Init();
PlatformHash_Update();
PlatformHash_Final();
```

上层只理解：

```text
SHA-256
```

不理解具体 Crypto Library。

---

# 21. Platform Boot Control

EEPROM Fast Update Check 非常简单。

不需要：

```text
BootControlService
BootControlRepository
BootControlAdapter
```

直接：

```text
PlatformBootControl
```

提供：

```c
bool PlatformBootControl_Read(
    BootControl_t *control);

bool PlatformBootControl_Write(
    const BootControl_t *control);

bool PlatformBootControl_Clear(void);
```

它内部：

```text
PlatformBootControl
    ↓
BSP_EEPROM
    ↓
AT24 Driver
```

即可。

---

# 22. BSP 层设计理念

BSP 的定义：

> 描述“这块板子上的硬件如何连接”。

BSP 可以知道：

- QSPI 实例；
- EEPROM I2C 地址；
- SDMMC 实例；
- Therapy BOOT GPIO；
- Therapy RESET GPIO；
- Therapy UART；
- 卡检测 GPIO。

BSP 不应知道：

- Manifest；
- UPDATE；
- CURRENT；
- firmware version；
- package_id。

---

# 23. Driver 层设计理念

Driver 只理解芯片或协议。

例如：

```text
SPI NOR Driver
```

理解：

- Read ID；
- Read；
- Page Program；
- Sector Erase；
- Status Register。

不知道：

```text
APP
GUI
firmware
CURRENT
```

---

# 24. Platform 与 BSP 的区别

最容易混淆的两个概念必须严格区分。

## Platform

代表：

> “软件需要什么能力”。

例如：

```text
读取文件
写 Flash
计算 SHA
系统复位
读取 BootControl
```

---

## BSP

代表：

> “当前板子如何实现这个能力”。

例如：

```text
W25Q256连接QUADSPI
EEPROM连接I2C1
Therapy BOOT为PC7
Therapy RESET为PG14
```

---

# 25. 推荐依赖方向

必须遵守：

```text
Application
    ↓
Services
    ↓
Platform
    ↓
BSP
    ↓
Drivers
    ↓
HAL / Middleware
```

允许：

```text
Application → Services
Application → Platform

Services → Platform

Platform → BSP

BSP → Drivers

Drivers → HAL
```

---

# 26. 禁止依赖

禁止：

```text
BSP → Services
```

禁止：

```text
Driver → Platform
```

禁止：

```text
Platform → Application
```

禁止：

```text
Manifest → BSP
```

禁止：

```text
VersionPolicy → FatFs
```

禁止：

```text
ImageInstaller → HAL
```

禁止：

```text
AppLauncher → FatFs
```

---

# 27. 依赖规则的本质

高层业务：

```text
知道“要做什么”
```

底层硬件：

```text
知道“怎么操作硬件”
```

上层不能依赖硬件细节。

底层不能依赖业务语义。

---

# 28. 推荐最终目录

```text
Bootloader/
│
├── Application/
│   ├── boot_manager.c
│   ├── boot_manager.h
│   ├── update_manager.c
│   ├── update_manager.h
│   ├── recovery_manager.c
│   └── recovery_manager.h
│
├── Services/
│   ├── update_request.c
│   ├── update_request.h
│   ├── manifest.c
│   ├── manifest.h
│   ├── version_policy.c
│   ├── version_policy.h
│   ├── image_installer.c
│   ├── image_installer.h
│   ├── current_manager.c
│   ├── current_manager.h
│   ├── app_launcher.c
│   ├── app_launcher.h
│   ├── therapy_updater.c
│   └── therapy_updater.h
│
├── Platform/
│   ├── platform_storage.c
│   ├── platform_storage.h
│   ├── platform_flash.c
│   ├── platform_flash.h
│   ├── platform_hash.c
│   ├── platform_hash.h
│   ├── platform_boot_control.c
│   ├── platform_boot_control.h
│   ├── platform_watchdog.c
│   ├── platform_watchdog.h
│   ├── platform_reset.c
│   └── platform_reset.h
│
├── BSP/
│   ├── bsp_qspi.c
│   ├── bsp_qspi.h
│   ├── bsp_sd.c
│   ├── bsp_sd.h
│   ├── bsp_eeprom.c
│   ├── bsp_eeprom.h
│   ├── bsp_uart.c
│   ├── bsp_uart.h
│   ├── bsp_therapy_mcu.c
│   └── bsp_therapy_mcu.h
│
├── Drivers/
│   ├── spi_nor/
│   ├── at24/
│   └── stm32_rom_boot/
│
├── Middleware/
│   └── FatFs/
│
├── Common/
│   ├── boot_config.h
│   ├── boot_types.h
│   ├── firmware_layout.h
│   ├── firmware_types.h
│   └── error_code.h
│
└── Core/
    └── main.c
```

---

# 29. 推荐调用链

## 29.1 正常启动

```text
main
 ↓
BootManager
 ↓
PlatformBootControl
 ↓
AppLauncher
 ↓
PlatformFlash
 ↓
BSP_QSPI
```

---

## 29.2 正常升级

```text
BootManager
 ↓
UpdateManager
 ↓
UpdateRequest
 ↓
Manifest
 ↓
VersionPolicy
 ↓
ImageInstaller
 ↓
PlatformStorage / PlatformFlash / PlatformHash
```

---

## 29.3 CURRENT 提交

```text
UpdateManager
 ↓
CurrentManager
 ↓
PlatformStorage
 ↓
FatFs
```

---

## 29.4 Recovery

```text
BootManager
 ↓
RecoveryManager
 ↓
Manifest
 ↓
ImageInstaller
 ↓
Platform
```

---

## 29.5 Therapy

```text
UpdateManager
 ↓
TherapyUpdater
 ↓
PlatformStorage
 ↓
BSP_Therapy
 ↓
STM32 ROM Boot Driver
```

---

# 30. 模块是否应该抽象的判断标准

新增模块时先问三个问题。

## 30.1 是否隔离业务与硬件？

如果是：

```text
建议抽象
```

例如：

```text
PlatformFlash
PlatformStorage
```

---

## 30.2 是否存在两种以上合理实现？

如果是：

```text
可以抽象
```

例如：

```text
SHA256软件实现
SHA256硬件实现
```

---

## 30.3 是否只是增加一层转调？

如果是：

```text
不要抽象
```

例如：

```c
UpdateService_Write()
{
    ImageInstaller_Write();
}
```

如果没有额外业务语义，这一层应该删除。

---

# 31. “薄层”原则

每一层都必须提供明确价值。

不允许出现大量：

```c
A()
{
    return B();
}
```

如果一个模块长期只有纯转发，没有：

- 状态；
- 业务规则；
- 资源管理；
- 错误转换；
- 生命周期控制；

则应考虑删除。

---

# 32. 数据结构设计原则

跨层数据结构应优先使用业务类型。

例如：

```c
FirmwareManifest_t
ImageDescriptor_t
UpdateRequest_t
ImageInstallPlan_t
BootControl_t
```

不应把底层类型向上传播：

```c
FIL
QSPI_HandleTypeDef
I2C_HandleTypeDef
UART_HandleTypeDef
HAL_StatusTypeDef
```

这些类型应限制在 Platform/BSP/Driver 范围。

---

# 33. 错误码设计原则

建议定义统一业务错误码：

```c
typedef enum
{
    BOOT_OK = 0,

    BOOT_ERR_REQUEST,
    BOOT_ERR_MANIFEST,
    BOOT_ERR_VERSION,
    BOOT_ERR_FILE,
    BOOT_ERR_HASH,
    BOOT_ERR_ERASE,
    BOOT_ERR_WRITE,
    BOOT_ERR_READBACK,
    BOOT_ERR_RECOVERY,
    BOOT_ERR_LAUNCH,

} BootError_t;
```

底层错误：

```text
HAL_ERROR
FR_DISK_ERR
SPI timeout
```

应在 Platform/BSP 转换成上层可理解的错误。

上层不应该依赖 FatFs 或 HAL 错误码。

---

# 34. 状态管理原则

状态只存在于真正拥有生命周期的模块。

例如：

```text
UpdateManager
RecoveryManager
CurrentManager
```

可以有状态。

而：

```text
VersionPolicy
Manifest parser
SHA wrapper
```

应尽量保持无状态或短生命周期。

---

# 35. 全局变量原则

禁止跨模块直接访问全局状态。

例如不推荐：

```c
extern bool g_update_requested;
extern FIL g_manifest_file;
extern uint8_t g_sha_buffer[];
```

应由拥有该资源的模块管理。

跨模块通过接口传递。

---

# 36. 配置原则

产品配置集中在：

```text
Common/boot_config.h
Common/firmware_layout.h
```

例如：

```c
#define BOOT_FAST_UPDATE_CHECK_ENABLE 1

#define BOOT_PRODUCT_NAME
#define BOOT_HARDWARE_NAME

#define BOOT_APP_MAX_SIZE
#define BOOT_GUI_MAX_SIZE
```

避免散落在多个 `.c` 文件。

---

# 37. 内存原则

Bootloader 应尽量使用：

- 静态内存；
- 固定 Buffer；
- 明确生命周期对象。

默认不推荐：

```text
malloc
free
```

原因：

- 生命周期短；
- 内存需求固定；
- 动态内存没有明显收益；
- 增加不可预测性。

---

# 38. 阻塞与状态机原则

当前项目为裸机。

对于：

```text
较短文件操作
Flash页写
Hash Update
```

可采用同步调用。

对于明显耗时、需要喂狗或外设轮询的流程：

```text
大文件安装
Therapy ROM Boot
CURRENT复制
```

可由 UpdateManager / RecoveryManager 管理状态。

但不应为了“异步”把每个函数都改成复杂状态机。

原则：

> 只有真正长时间操作才需要状态机。

---

# 39. 看门狗设计原则

看门狗属于 Platform 能力。

业务层只表达：

```text
继续运行
Fatal
Reset
```

业务模块不应该直接：

```c
HAL_IWDG_Refresh();
```

推荐：

```c
PlatformWatchdog_Kick();
```

Fatal 时：

```text
停止业务推进
停止正常喂狗
等待复位
```

---

# 40. 日志设计原则

日志可被所有高层模块使用。

但日志模块不能反向依赖业务。

推荐：

```c
LOG_INFO(...)
LOG_WARN(...)
LOG_ERROR(...)
```

业务模块打印：

```text
Update start
Manifest invalid
Recovery start
```

底层 Driver 只打印必要的硬件错误。

---

# 41. 可测试性原则

本架构不为了 Unit Test 引入完整 Interface/Adapter。

但纯业务模块应天然可测试：

```text
Manifest
UpdateRequest
VersionPolicy
```

这些模块不依赖 HAL，Host Test 可以直接编译。

对于 ImageInstaller 等依赖 Platform 的模块，如果未来确实需要复杂 Mock，再针对性引入测试替身。

原则：

> 先保持产品代码简单，有实际测试需求时再增加抽象。

---

# 42. 架构演进原则

只有出现真实需求时才增加层级。

例如未来出现：

```text
SD
+
eMMC
```

可扩展 PlatformStorage。

未来出现：

```text
QSPI NOR
+
OSPI NOR
```

可扩展 PlatformFlash/BSP。

未来出现：

```text
多个 MCU 平台
```

再考虑引入更正式的 Hardware Interface。

在需求不存在时，不提前构建复杂扩展框架。

---

# 43. 不推荐的架构模式

本项目明确避免：

```text
Manager → Service → Interface → Adapter → Platform → BSP
```

这种过长链路。

也避免：

```text
一个功能一个 Manager
一个 Manager 一个 Service
一个 Service 一个 Adapter
```

以及大量：

```text
xxx_provider
xxx_repository
xxx_gateway
xxx_facade
```

除非模块确实承担对应职责。

---

# 44. 最终设计哲学

本项目架构的核心不是“层数”，而是“边界”。

边界必须明确：

```text
Application
    管流程

Services
    管业务

Platform
    管能力

BSP
    管板卡

Driver
    管芯片/协议
```

---

# 45. 最终原则总结

整个 Bootloader 应始终遵守：

```text
1. 业务代码不直接调用 HAL。

2. 业务代码不直接调用 FatFs。

3. BSP 不理解 UPDATE、CURRENT、Manifest。

4. Driver 不理解产品业务。

5. Platform 是业务与硬件之间唯一主要能力边界。

6. 纯业务模块不额外套 Interface。

7. 只有真实的可替换能力才值得抽象。

8. 不为未来假设需求提前设计复杂框架。

9. 调用链越短越好，但不能破坏职责边界。

10. 能用一个明确模块解决的问题，不拆成三个纯转发模块。

11. 正常升级和 Recovery 共用底层 ImageInstaller。

12. Version Policy 只属于正常升级，不属于 Recovery。

13. CURRENT 文件事务由 CurrentManager 独立负责。

14. APP Jump 由 AppLauncher 独立负责。

15. EEPROM Fast Update Check 只是 Platform 能力，不建立复杂 BootControl Service。

16. 配置集中、依赖单向、数据结构业务化、底层类型不上泄。
```

---

# 46. 结论

针对当前 STM32H7 HMI Bootloader，推荐采用：

```text
Application
    ↓
Services
    ↓
Platform
    ↓
BSP / Drivers
```

这是当前需求下最平衡的架构。

它既避免：

```text
Application
直接调用 HAL / FatFs
```

造成的强耦合，也避免：

```text
Interfaces
Adapters
Repositories
Providers
Composition
```

带来的过度设计。

最终目标是：

> **模块职责明确、依赖方向单一、硬件细节隔离、业务代码可读、调用链短、后续维护成本低。**

对于 Bootloader 这种强调稳定性、确定性和长期维护的软件，这比追求复杂的架构形式更重要。
