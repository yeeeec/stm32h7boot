# STM32H7 HMI Bootloader Platform 与 BSP 软件需求规格说明书

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 文档名称 | STM32H7 HMI Bootloader Platform 与 BSP 软件需求规格说明书 |
| 文档类型 | Software Requirements Specification（SRS） |
| 目标平台 | STM32H743 |
| Bootloader | 裸机 Bootloader |
| 外部 Flash | W25Q256 QSPI NOR Flash |
| 升级介质 | SD Card + FatFs |
| 参数存储 | AT24 EEPROM |
| 外部 MCU | Therapy MCU，STM32 ROM Boot 升级 |
| 文档版本 | 1.0 |
| 状态 | Baseline |

---

## 2. 目的

本文档定义 Bootloader 中 `Platform` 与 `BSP` 两层的软件需求。

设计目标：

- 上层业务不直接依赖 HAL、LL、FatFs；
- Platform 只描述“软件需要什么能力”；
- BSP 只描述“当前板卡硬件如何连接和访问”；
- Driver 只描述“具体器件或协议如何工作”；
- 避免 Interface / Adapter / Repository 等无必要封装；
- 保持依赖单向、调用链短、职责清晰。

---

## 3. 架构位置

```text
Application
    ↓
Services
    ↓
Platform
    ↓
BSP
    ↓
Drivers / Middleware
    ↓
STM32 HAL / LL / CMSIS
```

允许部分 Platform 模块直接依赖 HAL/CMSIS：

```text
PlatformReset   → CMSIS
PlatformTime    → HAL
PlatformWatchdog→ HAL
```

并非所有 Platform 模块都必须经过 BSP。

---

# 4. 基本定义

## 4.1 Platform

Platform 的定义：

> 向 Application / Services 提供稳定的软件能力，并隐藏中间件、HAL 和板级细节。

Platform 可以提供：

```text
文件访问
Flash访问
SHA-256
BootControl持久化
Watchdog
Reset
Time
Log
```

Platform 不应该知道：

```text
APP升级流程
GUI升级流程
UPDATE事务
CURRENT事务
Recovery策略
Version Policy
```

---

## 4.2 BSP

BSP 的定义：

> 描述当前 PCB 上具体硬件资源如何连接、初始化和访问。

BSP 可以知道：

```text
QUADSPI实例
SDMMC实例
I2C实例
EEPROM地址
Therapy BOOT GPIO
Therapy RESET GPIO
UART实例
板级时钟/GPIO连接
```

BSP 不应该知道：

```text
Manifest
UPDATE
CURRENT
package_id
Version Policy
Recovery
```

---

## 4.3 Driver

Driver 的定义：

> 描述具体器件或通信协议本身。

例如：

```text
W25Q256命令
AT24页读写
STM32 ROM Boot协议
```

---

# 5. 总体设计规则

### GEN-001
Application / Services 不得直接调用 STM32 HAL。

### GEN-002
Application / Services 不得直接调用 FatFs。

### GEN-003
Platform 公共接口不得暴露 HAL 类型。

禁止向上暴露：

```c
QSPI_HandleTypeDef
SD_HandleTypeDef
I2C_HandleTypeDef
UART_HandleTypeDef
HAL_StatusTypeDef
```

### GEN-004
Platform 公共接口不得暴露 FatFs 类型。

禁止向上暴露：

```c
FIL
FRESULT
FATFS
DIR
```

### GEN-005
BSP 公共接口不得暴露业务类型。

例如不得接收：

```c
FirmwareManifest_t *
UpdateRequest_t *
```

### GEN-006
BSP 不得调用 Platform、Services、Application。

### GEN-007
Platform 不得调用 Services、Application。

### GEN-008
Driver 不得理解产品业务。

### GEN-009
不应仅为了分层形式增加纯函数转发模块。

### GEN-010
每个 Platform/BSP 模块必须有明确职责，不允许形成巨大 `platform.c` 或 `bsp.c`。

---

# 6. 推荐目录

```text
Platform/
├── include/
│   └── platform/
│       ├── platform_storage.h
│       ├── platform_flash.h
│       ├── platform_hash.h
│       ├── platform_boot_control.h
│       ├── platform_watchdog.h
│       ├── platform_reset.h
│       ├── platform_time.h
│       └── platform_log.h
└── src/
    ├── platform_storage.c
    ├── platform_flash.c
    ├── platform_hash.c
    ├── platform_boot_control.c
    ├── platform_watchdog.c
    ├── platform_reset.c
    ├── platform_time.c
    └── platform_log.c

BSP/
├── include/
│   └── bsp/
│       ├── bsp_qspi.h
│       ├── bsp_sd.h
│       ├── bsp_eeprom.h
│       ├── bsp_uart.h
│       └── bsp_therapy.h
└── src/
    ├── bsp_qspi.c
    ├── bsp_sd.c
    ├── bsp_eeprom.c
    ├── bsp_uart.c
    └── bsp_therapy.c

Drivers/
├── spi_nor/
├── at24/
└── stm32_rom_boot/
```

---

# 7. Platform 公共错误码

推荐统一：

```c
typedef enum
{
    PLATFORM_OK = 0,
    PLATFORM_ERROR,
    PLATFORM_INVALID_ARG,
    PLATFORM_TIMEOUT,
    PLATFORM_BUSY,
    PLATFORM_NOT_FOUND,
    PLATFORM_IO_ERROR,
    PLATFORM_NOT_READY,
    PLATFORM_UNSUPPORTED
} PlatformResult_t;
```

### PLAT-ERR-001
Platform 应将 HAL/FatFs/BSP/Driver 错误转换为 PlatformResult_t。

### PLAT-ERR-002
Services 不应根据 `FR_xxx` 或 `HAL_xxx` 做业务判断。

### PLAT-ERR-003
必要时可通过独立诊断日志记录底层原始错误码。

---

# 8. PlatformStorage

## 8.1 职责

PlatformStorage 负责：

```text
Mount
Unmount
Exists
OpenRead
OpenWrite
Read
Write
Close
GetSize
Remove
Rename
Mkdir
Sync
```

其内部实现可直接使用 FatFs。

---

## 8.2 公共接口建议

```c
typedef struct PlatformFile PlatformFile_t;

PlatformResult_t PlatformStorage_Mount(void);
PlatformResult_t PlatformStorage_Unmount(void);

bool PlatformStorage_Exists(const char *path);

PlatformResult_t PlatformStorage_OpenRead(
    PlatformFile_t *file,
    const char *path);

PlatformResult_t PlatformStorage_OpenWrite(
    PlatformFile_t *file,
    const char *path);

PlatformResult_t PlatformStorage_Read(
    PlatformFile_t *file,
    void *buffer,
    size_t capacity,
    size_t *actual_size);

PlatformResult_t PlatformStorage_Write(
    PlatformFile_t *file,
    const void *buffer,
    size_t size);

PlatformResult_t PlatformStorage_Close(
    PlatformFile_t *file);

PlatformResult_t PlatformStorage_GetSize(
    const char *path,
    uint32_t *size);

PlatformResult_t PlatformStorage_Remove(
    const char *path);

PlatformResult_t PlatformStorage_Rename(
    const char *old_path,
    const char *new_path);

PlatformResult_t PlatformStorage_Mkdir(
    const char *path);

PlatformResult_t PlatformStorage_Sync(
    PlatformFile_t *file);
```

---

## 8.3 需求

### PLAT-STG-001
PlatformStorage 不得解析 Manifest、Request 或其他业务文件格式。

### PLAT-STG-002
PlatformStorage 不得提供：

```c
ReadManifest();
ReadRequest();
CopyCurrentFirmware();
```

### PLAT-STG-003
PlatformStorage 应支持大文件流式读写。

### PLAT-STG-004
PlatformStorage 不应强制一次性把整个文件读入 RAM。

### PLAT-STG-005
关键写入操作必须支持显式 Sync。

### PLAT-STG-006
文件句柄生命周期必须明确：Open 成功后必须由调用方 Close。

### PLAT-STG-007
Mount 失败应返回明确错误，不得自动进入业务 Recovery。

### PLAT-STG-008
PlatformStorage 不负责决定“Mount失败后是否启动APP”。

---

# 9. PlatformFlash

## 9.1 职责

PlatformFlash 向上提供通用 Runtime Flash 能力。

---

## 9.2 公共接口建议

```c
PlatformResult_t PlatformFlash_Init(void);

PlatformResult_t PlatformFlash_Read(
    uint32_t address,
    void *buffer,
    uint32_t size);

PlatformResult_t PlatformFlash_Write(
    uint32_t address,
    const void *buffer,
    uint32_t size);

PlatformResult_t PlatformFlash_Erase(
    uint32_t address,
    uint32_t size);

PlatformResult_t PlatformFlash_EnterXip(void);
PlatformResult_t PlatformFlash_ExitXip(void);

bool PlatformFlash_IsXipEnabled(void);
```

---

## 9.3 需求

### PLAT-FLS-001
PlatformFlash 不得理解 APP/GUI 业务。

禁止：

```c
PlatformFlash_WriteApp();
PlatformFlash_EraseGui();
```

### PLAT-FLS-002
APP/GUI 地址和大小应由 Services/Common 传入。

### PLAT-FLS-003
PlatformFlash 应隐藏：

```text
页大小
Sector大小
Erase Command
Program Command
Busy轮询
器件ID
```

### PLAT-FLS-004
Erase/Write 前应保证 Flash 处于允许写入的模式。

### PLAT-FLS-005
Memory-Mapped/XIP 状态切换应有明确接口。

### PLAT-FLS-006
所有 Busy 等待必须有超时。

### PLAT-FLS-007
禁止底层出现无限等待。

### PLAT-FLS-008
PlatformFlash 可调用 BSP_QSPI。

---

# 10. PlatformHash

## 10.1 公共接口建议

```c
typedef struct PlatformHashContext PlatformHashContext_t;

PlatformResult_t PlatformHash_Init(
    PlatformHashContext_t *ctx);

PlatformResult_t PlatformHash_Update(
    PlatformHashContext_t *ctx,
    const void *data,
    size_t size);

PlatformResult_t PlatformHash_Final(
    PlatformHashContext_t *ctx,
    uint8_t digest[32]);
```

## 10.2 需求

### PLAT-HASH-001
应支持 SHA-256 流式计算。

### PLAT-HASH-002
Services 不应感知 SHA 实现来自软件库还是硬件外设。

### PLAT-HASH-003
Hash Context 不应暴露底层第三方库类型。

### PLAT-HASH-004
更换 SHA 实现不应修改 Manifest/ImageInstaller 业务接口。

---

# 11. PlatformBootControl

## 11.1 职责

PlatformBootControl 负责 BootControl 在 EEPROM 中的存取与结构完整性校验。

---

## 11.2 BootControl

示例：

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

---

## 11.3 公共接口建议

```c
PlatformResult_t PlatformBootControl_Read(
    BootControl_t *control);

PlatformResult_t PlatformBootControl_Write(
    const BootControl_t *control);

PlatformResult_t PlatformBootControl_Clear(void);
```

---

## 11.4 需求

### PLAT-BC-001
PlatformBootControl 应校验：

```text
magic
format_version
request取值
check/CRC
```

### PLAT-BC-002
PlatformBootControl 可理解 BootControl 数据格式。

### PLAT-BC-003
PlatformBootControl 不得实现：

```c
ShouldUpgrade();
ShouldBootApp();
ShouldRecover();
```

### PLAT-BC-004
是否升级由 Application 决定。

### PLAT-BC-005
PlatformBootControl 通过 BSP_EEPROM 读写原始字节。

### PLAT-BC-006
EEPROM 物理地址不应由 Application/Services 使用。

---

# 12. PlatformWatchdog

建议接口：

```c
PlatformResult_t PlatformWatchdog_Init(void);
void PlatformWatchdog_Refresh(void);
```

### PLAT-WDG-001
Application/Services 不得直接调用 `HAL_IWDG_Refresh()`。

### PLAT-WDG-002
PlatformWatchdog 可以直接调用 HAL。

### PLAT-WDG-003
PlatformWatchdog 不负责决定 Fatal 时是否继续喂狗。

### PLAT-WDG-004
“停止喂狗”是 Application 的状态策略。

---

# 13. PlatformReset

建议：

```c
void PlatformReset_System(void);
```

### PLAT-RST-001
PlatformReset 可直接调用 CMSIS `NVIC_SystemReset()`。

### PLAT-RST-002
不要求额外建立 BSP_Reset。

### PLAT-RST-003
PlatformReset 不处理业务状态清理。

---

# 14. PlatformTime

建议：

```c
uint32_t PlatformTime_GetMs(void);
void PlatformTime_DelayMs(uint32_t ms);
```

### PLAT-TIME-001
PlatformTime 可以直接使用 HAL Tick。

### PLAT-TIME-002
时间比较必须正确处理 uint32_t Tick 回绕。

### PLAT-TIME-003
长耗时硬件轮询必须基于超时判断。

---

# 15. PlatformLog

建议：

```c
void PlatformLog_Info(const char *fmt, ...);
void PlatformLog_Warn(const char *fmt, ...);
void PlatformLog_Error(const char *fmt, ...);
```

或统一宏：

```c
LOG_INFO(...)
LOG_WARN(...)
LOG_ERROR(...)
```

### PLAT-LOG-001
日志上层不得知道具体 USART 实例。

### PLAT-LOG-002
PlatformLog 可通过 BSP_UART 输出。

### PLAT-LOG-003
日志失败不得影响核心 Boot 流程。

---

# 16. BSP 总体要求

### BSP-GEN-001
BSP 只描述当前板卡的硬件连接和访问。

### BSP-GEN-002
BSP 不得解析 JSON。

### BSP-GEN-003
BSP 不得执行 Version Policy。

### BSP-GEN-004
BSP 不得操作 UPDATE/CURRENT 目录。

### BSP-GEN-005
BSP 不得直接调用 Platform。

### BSP-GEN-006
BSP 可调用 Driver/HAL/LL。

### BSP-GEN-007
BSP API 不得出现产品业务函数名。

---

# 17. BSP_QSPI

## 17.1 职责

负责：

```text
当前板卡QSPI资源
Flash器件绑定
GPIO/Clock
底层读写擦除
Memory-Mapped状态切换
```

---

## 17.2 接口建议

```c
BSPResult_t BSP_QSPI_Init(void);

BSPResult_t BSP_QSPI_Read(
    uint32_t address,
    void *buffer,
    uint32_t size);

BSPResult_t BSP_QSPI_Write(
    uint32_t address,
    const void *buffer,
    uint32_t size);

BSPResult_t BSP_QSPI_Erase(
    uint32_t address,
    uint32_t size);

BSPResult_t BSP_QSPI_EnterMemoryMapped(void);
BSPResult_t BSP_QSPI_ExitMemoryMapped(void);
```

---

## 17.3 需求

### BSP-QSPI-001
BSP_QSPI 可以知道 W25Q256。

### BSP-QSPI-002
BSP_QSPI 可以知道 QUADSPI 外设实例。

### BSP-QSPI-003
BSP_QSPI 不得知道 APP/GUI 分区语义。

### BSP-QSPI-004
BSP_QSPI 不得实现固件安装流程。

### BSP-QSPI-005
底层器件命令优先由 SPI NOR Driver 实现。

---

# 18. BSP_SD

建议：

```c
BSPResult_t BSP_SD_Init(void);
BSPResult_t BSP_SD_DeInit(void);
bool BSP_SD_IsPresent(void);
```

### BSP-SD-001
BSP_SD 只负责 SD 硬件资源。

### BSP-SD-002
BSP_SD 不得提供文件 API。

禁止：

```c
BSP_SD_OpenFile();
BSP_SD_ReadManifest();
```

### BSP-SD-003
FatFs 文件语义必须位于 PlatformStorage。

### BSP-SD-004
SD 卡检测如存在 GPIO，应由 BSP_SD 封装。

---

# 19. BSP_EEPROM

建议：

```c
BSPResult_t BSP_EEPROM_Init(void);

BSPResult_t BSP_EEPROM_Read(
    uint16_t address,
    void *buffer,
    uint16_t size);

BSPResult_t BSP_EEPROM_Write(
    uint16_t address,
    const void *buffer,
    uint16_t size);
```

### BSP-EEP-001
BSP_EEPROM 只提供 EEPROM 原始字节访问。

### BSP-EEP-002
BSP_EEPROM 可知道：

```text
I2C实例
EEPROM器件地址
Page Size
Write Cycle
```

### BSP-EEP-003
BSP_EEPROM 不得理解 BootControl_t。

### BSP-EEP-004
BSP_EEPROM 不得理解 `BOOT_REQUEST_UPDATE`。

### BSP-EEP-005
AT24 页边界和写周期可交由 AT24 Driver 处理。

---

# 20. BSP_UART

建议：

```c
BSPResult_t BSP_UART_Init(void);
BSPResult_t BSP_UART_Write(
    const void *data,
    uint32_t size);
```

### BSP-UART-001
BSP_UART 负责板级 UART 资源。

### BSP-UART-002
BSP_UART 不负责 INFO/WARN/ERROR 策略。

### BSP-UART-003
BSP_UART 不理解升级状态。

---

# 21. BSP_Therapy

## 21.1 职责

负责：

```text
BOOT0 GPIO
RESET GPIO
Therapy UART板级配置
进入ROM Boot所需硬件状态
恢复Application UART配置
```

---

## 21.2 接口建议

```c
void BSP_Therapy_SetBootMode(bool enable);
void BSP_Therapy_Reset(void);

BSPResult_t BSP_Therapy_SetUartBootMode(void);
BSPResult_t BSP_Therapy_SetUartApplicationMode(void);

BSPResult_t BSP_Therapy_UartWrite(
    const void *data,
    uint32_t size);

BSPResult_t BSP_Therapy_UartRead(
    void *data,
    uint32_t size,
    uint32_t timeout_ms);
```

### BSP-THER-001
BSP_Therapy 不得实现 STM32 ROM Boot 命令。

### BSP-THER-002
`GET/ERASE/WRITE MEMORY/READ MEMORY` 属于 `stm32_rom_boot` Driver。

### BSP-THER-003
BSP_Therapy 只负责板级 GPIO/UART。

---

# 22. Driver 要求

## 22.1 SPI NOR Driver

负责：

```text
Read ID
Read
Page Program
Sector/Block Erase
Status Register
Wait Busy
```

不得理解：

```text
APP
GUI
firmware
```

---

## 22.2 AT24 Driver

负责：

```text
Address
Page Read
Page Write
Page Boundary
Write Cycle
```

不得理解 BootControl。

---

## 22.3 STM32 ROM Boot Driver

负责：

```text
SYNC
GET
GET VERSION
GET ID
READ MEMORY
WRITE MEMORY
ERASE
GO
ACK/NACK
协议Checksum
```

不得理解 Therapy 固件版本策略。

---

# 23. 初始化策略

### INIT-001
Bootloader 应采用按需初始化。

### INIT-002
EEPROM Fast Check 开启时，应优先只初始化 EEPROM 所需的最小硬件。

### INIT-003
EEPROM 为 NONE 时，不应初始化 SD/FatFs。

### INIT-004
仅需要检查升级时才初始化 SD/FatFs。

### INIT-005
QSPI 必须在 AppLauncher 使用前处于可用状态。

### INIT-006
Therapy ROM Boot GPIO/UART 只在 Therapy 升级时配置为 Boot 模式。

---

# 24. 裸机与超时要求

### BARE-001
Platform/BSP 不依赖 RTOS API。

### BARE-002
所有底层 Busy/ACK/Ready 轮询必须有超时。

### BARE-003
不得出现无限硬件等待。

### BARE-004
长时间擦除、复制、Therapy 升级必须允许系统周期性处理 Watchdog。

### BARE-005
BSP/Driver 不应自行永久刷新 Watchdog。

---

# 25. 内存要求

### MEM-001
默认禁止在 Platform/BSP 使用动态内存。

### MEM-002
文件和 Flash 传输使用固定缓冲区流式处理。

### MEM-003
Buffer 优先由调用方提供。

### MEM-004
Platform/BSP 不应复制不必要的大块数据。

---

# 26. 路径与配置要求

### CFG-001
业务路径不得硬编码在 BSP。

例如：

```text
/UPDATE/
/CURRENT/
```

只能出现在 Application/Services/Common 配置。

### CFG-002
QSPI/EEPROM 等板级参数应集中在 BSP 或 Board Config。

### CFG-003
Runtime 地址布局建议放入：

```text
Common/firmware_layout.h
```

不放入 BSP。

---

# 27. 依赖规则

允许：

```text
Application → Services
Application → Platform

Services → Platform

Platform → BSP
Platform → HAL/CMSIS
Platform → Middleware

BSP → Drivers
BSP → HAL/LL

Drivers → HAL/LL
```

禁止：

```text
Platform → Services
Platform → Application

BSP → Platform
BSP → Services
BSP → Application

Drivers → BSP
Drivers → Platform
Drivers → Services
```

---

# 28. 典型调用链

## 28.1 文件

```text
Manifest / CurrentManager
    ↓
PlatformStorage
    ↓
FatFs
    ↓
diskio
    ↓
BSP_SD / HAL_SD
```

## 28.2 QSPI Runtime

```text
ImageInstaller
    ↓
PlatformFlash
    ↓
BSP_QSPI
    ↓
SPI NOR Driver
    ↓
HAL_QSPI
```

## 28.3 EEPROM

```text
BootManager
    ↓
PlatformBootControl
    ↓
BSP_EEPROM
    ↓
AT24 Driver
    ↓
HAL_I2C
```

## 28.4 Therapy

```text
TherapyUpdater
    ↓
STM32 ROM Boot Driver
    ↓
BSP_Therapy
    ↓
HAL_UART / GPIO
```

---

# 29. 明确禁止的接口

以下接口不得存在：

```c
BSP_QSPI_InstallApp();
BSP_QSPI_InstallGui();

BSP_SD_ReadManifest();
BSP_SD_CheckUpdate();

BSP_EEPROM_ShouldUpgrade();

PlatformFlash_InstallFirmware();
PlatformFlash_RestoreCurrent();

PlatformStorage_LoadManifest();

PlatformBootControl_ShouldUpgrade();
PlatformBootControl_ShouldRecover();
```

原因：这些接口跨越了层级职责。

---

# 30. 何时不需要 BSP

以下能力通常可由 Platform 直接调用 HAL/CMSIS：

```text
Reset
Tick/Time
Watchdog
部分Cache/Barrier操作
```

原则：

> BSP 只在“板级连接差异”真实存在时提供价值。

不要为了层级对称而增加：

```text
PlatformReset → BSP_Reset → CMSIS
```

无意义转发。

---

# 31. 何时必须经过 BSP

以下能力通常应经过 BSP：

```text
QSPI Flash
SD
EEPROM
板级UART
Therapy BOOT/RESET
板级电源/片选/卡检测
```

因为它们与 PCB 连接直接相关。

---

# 32. 验收标准

### AC-001
Services 源码中不得出现 `HAL_` 调用。

### AC-002
Services 源码中不得出现 `f_open/f_read/f_write/f_mount` 等 FatFs 调用。

### AC-003
Platform 公共头文件不得包含 STM32 HAL Handle 类型。

### AC-004
Platform 公共头文件不得包含 FatFs `FIL/FRESULT` 类型。

### AC-005
PlatformFlash 中不得存在 APP/GUI 专用写入接口。

### AC-006
PlatformBootControl 中不得存在 ShouldUpgrade 决策。

### AC-007
BSP 源码不得解析 JSON。

### AC-008
BSP_EEPROM 只提供原始存储访问，不理解 BootControl。

### AC-009
BSP_SD 不提供文件系统接口。

### AC-010
BSP_Therapy 不实现 STM32 ROM Boot 高层命令。

### AC-011
Driver 不出现 UPDATE/CURRENT/Manifest 业务名称。

### AC-012
依赖方向不存在反向依赖和循环依赖。

### AC-013
所有硬件轮询都有超时。

### AC-014
正常 Fast Boot 路径不初始化 SD/FatFs。

---

# 33. 最终职责表

| 层级 | 负责 | 不负责 |
|---|---|---|
| PlatformStorage | 文件能力 | Manifest业务 |
| PlatformFlash | Flash能力 | APP/GUI业务 |
| PlatformHash | SHA-256能力 | 固件策略 |
| PlatformBootControl | BootControl持久化 | 是否升级决策 |
| PlatformWatchdog | Watchdog能力 | Fatal策略 |
| PlatformReset | 系统复位 | 业务清理 |
| BSP_QSPI | 板级QSPI | 固件安装 |
| BSP_SD | 板级SD | 文件操作 |
| BSP_EEPROM | EEPROM raw I/O | BootControl业务 |
| BSP_UART | 板级UART | 日志等级 |
| BSP_Therapy | GPIO/UART板级控制 | ROM Boot协议 |
| Driver | 器件/协议 | 产品业务 |

---

# 34. 最终原则

```text
Platform = 软件能力
BSP      = 板级实现
Driver   = 器件/协议
```

必须长期保持：

```text
1. Services 不碰 HAL/FatFs。
2. Platform 不理解升级业务。
3. BSP 不理解 Manifest/UPDATE/CURRENT。
4. Driver 不理解 HMI 产品语义。
5. 不强制所有 Platform 都经过 BSP。
6. 有板级差异才建立 BSP。
7. 不建立无意义纯转发层。
8. API 稳定、依赖单向、错误统一、超时明确。
```

---

# 35. 结论

本 Bootloader 的 Platform/BSP 推荐边界为：

```text
Services
    ↓
Platform
    ↓
BSP
    ↓
Drivers
    ↓
HAL
```

其中：

> Platform 负责“软件需要什么能力”；  
> BSP 负责“当前板子如何提供这些硬件能力”。

这套设计在保持必要解耦的同时，不引入额外 Interfaces、Adapters、Repository 等架构层，适合当前 STM32H7 裸机 Bootloader 的固定硬件、固定文件系统和长期维护需求。
