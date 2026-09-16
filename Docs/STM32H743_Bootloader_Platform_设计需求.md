# STM32H743 HMI Bootloader Platform 设计需求

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 文档名称 | STM32H743 HMI Bootloader Platform 设计需求 |
| 目标MCU | STM32H743XIH6 |
| 软件形态 | 裸机Bootloader |
| 外部Flash | W25Q256 QSPI NOR Flash |
| 升级存储 | SD Card + FatFs |
| 状态存储 | AT24 EEPROM |
| 外部目标MCU | STM32H743XIH6，STM32 ROM Boot协议 |
| 日志接口 | USART1，仅发送 |
| ISP接口 | USART2，115200 bit/s |
| 文档状态 | Draft |

---

## 2. 目的

本文档定义当前唯一STM32H743 Bootloader工程中Platform层的职责、依赖关系、公共接口、初始化方式和验收要求。

本设计不追求跨产品、跨MCU或跨操作系统复用，不引入通用Interface、Adapter、Repository或依赖注入框架。

设计目标：

1. Bootloader业务代码不直接依赖STM32 HAL、CubeMX Handle或FatFs类型；
2. Platform向业务代码提供固定且直接的软件能力；
3. Platform负责组合BSP和通用Driver；
4. BSP只负责当前PCB上的外设连接和数据传输；
5. Driver只负责具体器件或通信协议；
6. 保持调用链短，不建立纯转发模块；
7. 所有模块统一使用`firmware_status_t`；
8. 不使用动态内存，不依赖RTOS。

---

## 3. 固定系统边界

当前Platform仅服务于以下固定组合：

```text
STM32H743XIH6
├── SDMMC1 + FatFs
├── QUADSPI + W25Q256
├── I2C + AT24 EEPROM
├── USART1日志输出
├── USART2外部STM32 ROM Boot通信
├── Therapy BOOT0 GPIO
├── Therapy NRST GPIO
└── IWDG / SysTick / NVIC / Cache
```

本版本不要求：

- 支持其他MCU；
- 支持多种SD或文件系统Backend；
- 支持多种SPI NOR器件；
- 支持运行时选择不同BSP；
- 支持RTOS；
- 支持动态注册多个存储设备；
- 支持模拟Platform实现；
- 支持USB Host文件访问；
- 支持显示模块。

---

## 4. 总体关系

本项目不是严格的五层直线关系。

正确关系为：

```text
Boot Application
    ├── Services
    └── Platform

Services
    └── Platform

Platform
    ├── BSP
    ├── Drivers
    ├── FatFs
    └── HAL/CMSIS（仅系统级能力）

BSP
    └── STM32 HAL / CubeMX

Drivers
    └── 由Platform绑定的硬件回调
```

Platform同时组合BSP与Driver。BSP和Driver不是固定的上下级关系。

### ARCH-001

Boot Application应负责启动、升级、恢复、复位和启动APP的流程决策。

### ARCH-002

Services应负责完整的业务操作，例如升级包检查、Manifest解析、版本判断、镜像安装和外部MCU升级。

### ARCH-003

Platform应提供当前STM32H743板卡的具体软件能力，并隐藏HAL、FatFs、BSP和Driver内部类型。

### ARCH-004

Platform可以同时包含BSP和Driver头文件，并在对应`platform_xxx.c`内部完成两者绑定。

### ARCH-005

Driver不得直接包含BSP头文件。Driver通过初始化时注册的回调访问硬件。

### ARCH-006

BSP不得调用Platform、Services或Boot Application。

### ARCH-007

不得为了保持形式上的层级对称而建立纯转发文件。

### ARCH-008

Boot Application可以直接调用Reset、Watchdog、BootControl和APP启动等简单Platform能力，不强制所有调用经过Services。

---

## 5. Platform模块划分

Platform固定包含：

```text
Platform/
├── include/platform/
│   ├── platform.h
│   ├── platform_storage.h
│   ├── platform_flash.h
│   ├── platform_boot_control.h
│   ├── platform_therapy.h
│   ├── platform_application.h
│   └── platform_system.h
└── src/
    ├── platform.c
    ├── platform_storage.c
    ├── platform_flash.c
    ├── platform_boot_control.c
    ├── platform_therapy.c
    ├── platform_application.c
    └── platform_system.c
```

不建立：

```text
platform_display.c
platform_log.c
platform_hash.c
platform_storage_facade.c
storage_backend_fatfs.c
```

删除原因：

| 模块 | 原因 |
|---|---|
| `platform_display` | Bootloader没有显示需求 |
| `platform_log` | `logging`直接使用`bsp_log_uart`输出回调 |
| `platform_hash` | 软件SHA-256直接由`firmware_crypto`提供 |
| `platform_storage_facade` | `platform_storage`已经是文件系统门面 |
| `storage_backend_fatfs` | 系统只有一个SD/FatFs实现 |

---

## 6. 公共类型与错误码

### PLAT-COM-001

所有Platform公共接口统一返回：

```c
#include "firmware/status.h"

firmware_status_t
```

不得定义重复的：

```text
PlatformResult_t
BSPResult_t
PlatformError_t
```

### PLAT-COM-002

Platform公共头文件不得暴露：

```text
HAL_StatusTypeDef
QSPI_HandleTypeDef
SD_HandleTypeDef
I2C_HandleTypeDef
UART_HandleTypeDef
FIL
FATFS
FRESULT
DIR
spi_nor_t
at24_t
stm32_rom_boot_t
```

### PLAT-COM-003

Platform应把HAL、FatFs和Driver状态转换为`firmware_status_t`。

### PLAT-COM-004

Platform不得使用动态内存；所有上下文、文件句柄表和Driver实例使用静态存储。

---

## 7. Platform总体初始化

### 7.1 职责

`platform.c`只负责必要的组合初始化，不负责初始化所有升级外设。

建议接口：

```c
firmware_status_t Platform_Init(void);
```

### PLAT-INIT-001

`Platform_Init()`只初始化正常启动路径必需的能力：

- 日志输出绑定；
- EEPROM/BootControl所需I2C能力；
- Platform内部静态状态。

### PLAT-INIT-002

`Platform_Init()`不得无条件初始化：

- SDMMC/FatFs；
- QSPI写入模式；
- USART2 ROM Boot模式；
- Therapy BOOT0升级状态。

### PLAT-INIT-003

SD、QSPI和Therapy接口应在业务需要时按需初始化。

### PLAT-INIT-004

`HAL_Init()`、`SystemClock_Config()`和基础GPIO初始化仍由`main.c`/CubeMX启动代码负责。

### PLAT-INIT-005

Platform初始化失败必须返回明确状态，不得在Platform内部进入永久循环。

---

## 8. PlatformStorage

### 8.1 定位

PlatformStorage负责提供SD卡上的FatFs文件和目录能力。

固定调用链：

```text
Services
    ↓
PlatformStorage
    ↓
FatFs
    ↓
sd_diskio.c
    ↓
HAL_SD / hsd1
```

本项目明确不建立`bsp_sd.c/h`。

### PLAT-STG-001

PlatformStorage直接调用FatFs API，并将`FRESULT`转换为`firmware_status_t`。

### PLAT-STG-002

SDMMC块读写由CubeMX/FatFs的`sd_diskio.c`完成，不得在Platform重复实现`HAL_SD_ReadBlocks()`或`HAL_SD_WriteBlocks()`封装。

### PLAT-STG-003

SDMMC初始化只能有一个所有者。当前设计规定由`PlatformStorage_Init()`调用：

```c
MX_SDMMC1_SD_Init();
MX_FATFS_Init();
```

`sd_diskio.c`不得再次重复初始化`hsd1`。

### PLAT-STG-004

PlatformStorage不得解析Manifest、版本、升级请求或固件组件业务。

### PLAT-STG-005

文件读写必须支持固定缓冲区流式处理，不得要求把整个文件读入RAM。

### PLAT-STG-006

PlatformStorage使用固定大小的内部句柄表，对上层暴露整数句柄，不暴露`FIL`和`DIR`。

建议公共类型：

```c
typedef uint32_t platform_file_handle_t;
typedef uint32_t platform_dir_handle_t;

typedef struct
{
    char name[128];
    uint32_t size;
    uint8_t is_directory;
} platform_dir_entry_t;
```

### 8.2 建议接口

```c
firmware_status_t PlatformStorage_Init(void);
firmware_status_t PlatformStorage_Mount(void);
firmware_status_t PlatformStorage_Unmount(void);

firmware_status_t PlatformStorage_OpenRead(
    const char *path,
    platform_file_handle_t *handle);

firmware_status_t PlatformStorage_Read(
    platform_file_handle_t handle,
    void *buffer,
    size_t size,
    size_t *bytes_read);

firmware_status_t PlatformStorage_Close(
    platform_file_handle_t handle);

firmware_status_t PlatformStorage_Stat(
    const char *path,
    platform_file_info_t *info);

firmware_status_t PlatformStorage_DirOpen(
    const char *path,
    platform_dir_handle_t *handle);

firmware_status_t PlatformStorage_DirRead(
    platform_dir_handle_t handle,
    platform_dir_entry_t *entry);

firmware_status_t PlatformStorage_DirClose(
    platform_dir_handle_t handle);
```

### PLAT-STG-007

第一版Bootloader默认只读SD，因此不提供：

```text
OpenWrite
Write
Mkdir
Rename
Remove
Sync
```

如果保留`CURRENT/CURRENT_NEW`写入和目录事务需求，才允许增加上述写接口；不得提前增加未使用接口。

### PLAT-STG-008

目录枚举必须支持上层检查升级目录中文件是否多余、缺失以及名称大小写是否一致。

### PLAT-STG-009

Unmount时应关闭所有遗留文件和目录句柄，并清空句柄表。

### PLAT-STG-010

若SDMMC使用DMA，`sd_diskio.c`必须处理STM32H7 D-Cache一致性和32字节Cache Line对齐；该处理不得放入业务层。

---

## 9. PlatformFlash

### 9.1 定位

PlatformFlash向上提供固定W25Q256 Runtime Flash能力，并负责组合：

```text
spi_nor Driver + bsp_qspi_flash
```

调用链：

```text
ImageInstaller
    ↓
PlatformFlash
    ↓
spi_nor
    ↓ 注册回调
bsp_qspi_flash
    ↓
HAL_QSPI / hqspi
```

### PLAT-FLS-001

PlatformFlash内部维护唯一的SPI NOR实例和固定W25Q256配置。

### PLAT-FLS-002

PlatformFlash初始化时应：

1. 初始化QUADSPI；
2. 构造`spi_nor_port_t`；
3. 把`BspQspiFlash_Command/Receive/Transmit`绑定为Driver回调；
4. 初始化SPI NOR Driver；
5. 读取并校验W25Q256 JEDEC ID；
6. 确保32MiB地址空间使用正确的4字节地址方式。

### PLAT-FLS-003

W25Q256指令、页边界、擦除粒度、Write Enable和Busy等待属于`spi_nor`，不得在Platform重复实现。

### PLAT-FLS-004

QSPI HAL命令转换属于`bsp_qspi_flash`，不得在Platform直接构造`QSPI_CommandTypeDef`。

### PLAT-FLS-005

PlatformFlash不得理解APP、GUI、Voice或Config组件语义。

### PLAT-FLS-006

所有地址和长度必须进行溢出及W25Q256容量边界检查。

### PLAT-FLS-007

擦除、写入和Busy等待必须有超时，不得无限轮询。

### PLAT-FLS-008

写入前必须退出Memory-Mapped模式；进入Memory-Mapped前必须确保没有未完成的擦写操作。

### PLAT-FLS-009

退出、进入Memory-Mapped或修改Runtime内容后，必须执行必要的D-Cache/I-Cache失效和数据/指令屏障。

### 9.2 建议接口

```c
firmware_status_t PlatformFlash_Init(void);

firmware_status_t PlatformFlash_Read(
    uint32_t address,
    void *buffer,
    uint32_t size);

firmware_status_t PlatformFlash_Write(
    uint32_t address,
    const void *buffer,
    uint32_t size);

firmware_status_t PlatformFlash_Erase(
    uint32_t address,
    uint32_t size);

firmware_status_t PlatformFlash_EnterMemoryMapped(void);
firmware_status_t PlatformFlash_ExitMemoryMapped(void);
```

### PLAT-FLS-010

大范围擦除应拆分为有限时长的扇区操作，使上层可以在扇区之间刷新看门狗。

---

## 10. PlatformBootControl

### 10.1 定位

PlatformBootControl负责BootControl在AT24 EEPROM中的持久化和结构完整性校验，并组合：

```text
at24 Driver + bsp_i2c
```

调用链：

```text
Boot Application
    ↓
PlatformBootControl
    ↓
at24
    ↓ 注册回调
bsp_i2c
    ↓
HAL_I2C
```

### PLAT-BC-001

PlatformBootControl可以理解BootControl结构、magic、format version和CRC。

### PLAT-BC-002

PlatformBootControl不得决定是否升级、是否Recovery或是否启动APP。

### PLAT-BC-003

AT24页写、页边界、器件地址和写周期等待由`at24` Driver负责。

### PLAT-BC-004

I2C Handle、HAL状态和超时转换由`bsp_i2c`负责。

### PLAT-BC-005

EEPROM物理地址只允许出现在PlatformBootControl私有实现或固定Board Config中，不得暴露给Boot Application或Services。

### PLAT-BC-006

读取到无效magic、format version或CRC时应返回明确错误，不得自动决定业务回退流程。

### 10.2 建议接口

```c
firmware_status_t PlatformBootControl_Init(void);

firmware_status_t PlatformBootControl_Read(
    boot_control_t *control);

firmware_status_t PlatformBootControl_Write(
    const boot_control_t *control);

firmware_status_t PlatformBootControl_Clear(void);
```

---

## 11. PlatformTherapy

### 11.1 定位

PlatformTherapy负责组合外部STM32H743XIH6的ROM Boot协议和板级BOOT0、NRST、USART2能力：

```text
stm32_rom_boot Driver
bsp_isp_uart
bsp_isp_gpio
```

调用链：

```text
TherapyUpdater
    ↓
PlatformTherapy
    ├── stm32_rom_boot
    ├── bsp_isp_uart
    └── bsp_isp_gpio
```

### PLAT-THR-001

PlatformTherapy负责：

- 控制目标进入ROM Boot模式；
- 把USART2切换为115200、8E1；
- 执行ROM Boot SYNC；
- 对外提供擦除、写入、读取和启动能力；
- 完成后控制目标复位；
- 把USART2恢复为115200、8N1 Application模式。

### PLAT-THR-002

ACK/NACK、命令码、地址校验和、数据校验和及ROM Boot报文格式由`stm32_rom_boot`负责。

### PLAT-THR-003

USART2 HAL收发和串口参数切换由`bsp_isp_uart`负责。

### PLAT-THR-004

BOOT0和NRST电平及时序由`bsp_isp_gpio`负责。

### PLAT-THR-005

PlatformTherapy不得理解Manifest、组件版本或固件源文件路径。

### PLAT-THR-006

所有ROM Boot命令必须有超时；不得永久等待ACK。

### 11.2 建议接口

```c
firmware_status_t PlatformTherapy_Init(void);
firmware_status_t PlatformTherapy_BeginUpdate(void);

firmware_status_t PlatformTherapy_GetId(uint16_t *device_id);
firmware_status_t PlatformTherapy_Erase(void);

firmware_status_t PlatformTherapy_Write(
    uint32_t address,
    const void *data,
    uint32_t size);

firmware_status_t PlatformTherapy_Read(
    uint32_t address,
    void *data,
    uint32_t size);

firmware_status_t PlatformTherapy_Start(uint32_t address);
firmware_status_t PlatformTherapy_EndUpdate(void);
```

---

## 12. PlatformApplication

### 12.1 定位

PlatformApplication负责验证并启动位于W25Q256 Memory-Mapped区域的HMI APP Runtime。

### PLAT-APP-001

启动前应确认PlatformFlash已经初始化且可进入Memory-Mapped模式。

### PLAT-APP-002

应检查APP Vector Table地址和对齐要求。

### PLAT-APP-003

应检查初始MSP位于STM32H743允许的SRAM范围。

### PLAT-APP-004

应检查Reset Handler位于配置的APP Runtime地址范围，并检查Thumb位。

### PLAT-APP-005

跳转前应：

1. 禁止中断；
2. 停止SysTick；
3. 禁止并清除NVIC中断；
4. 停止Bootloader仍在使用的DMA和外设；
5. 进入QSPI Memory-Mapped模式；
6. 执行必要Cache维护和Barrier；
7. 设置`SCB->VTOR`；
8. 设置MSP；
9. 跳转Reset Handler。

### PLAT-APP-006

PlatformApplication不得执行版本策略、Manifest校验或升级决策。

### 12.2 建议接口

```c
firmware_status_t PlatformApplication_Validate(
    uint32_t vector_address);

void PlatformApplication_Jump(
    uint32_t vector_address);
```

`PlatformApplication_Jump()`成功时不返回。

---

## 13. PlatformSystem

### 13.1 定位

PlatformSystem合并体量很小且同属MCU系统控制的Time、Reset和Watchdog能力，避免建立三个纯转发模块。

### 13.2 建议接口

```c
uint32_t PlatformSystem_GetMs(void);
void PlatformSystem_DelayMs(uint32_t delay_ms);

firmware_status_t PlatformSystem_WatchdogInit(void);
void PlatformSystem_WatchdogRefresh(void);

void PlatformSystem_Reset(void);
```

### PLAT-SYS-001

Tick可以直接使用`HAL_GetTick()`，Reset可以直接使用`NVIC_SystemReset()`，不建立对应BSP模块。

### PLAT-SYS-002

所有Tick时间差计算必须使用无符号减法，以正确处理`uint32_t`回绕。

### PLAT-SYS-003

Watchdog刷新时机由Boot Application或Services控制，Platform不得自行永久刷新看门狗。

### PLAT-SYS-004

Flash擦除、镜像复制、Hash计算和ROM Boot升级必须在有限处理单元之间允许调用`PlatformSystem_WatchdogRefresh()`。

### PLAT-SYS-005

不可恢复错误由Boot Application决定停止喂狗或执行系统复位。

---

## 14. 日志与密码能力

### 14.1 日志

不建立PlatformLog。

固定调用关系：

```text
Boot Application / Services / Platform / Drivers
    ↓
logging
    ↓ 注册输出回调
bsp_log_uart
    ↓
USART1 TX
```

日志初始化绑定可以在`Platform_Init()`中完成，但不提供一组重复的`PlatformLog_Info/Warn/Error`转发函数。

日志失败不得改变核心Boot流程结果。

### 14.2 SHA-256和签名验证

不建立PlatformHash。

固定调用关系：

```text
Services
    ↓
firmware_crypto
```

Manifest签名验证、SHA-256和ECDSA P-256属于`firmware_crypto`及业务验证模块，不属于Platform、BSP或Driver。

---

## 15. 初始化和启动顺序

建议启动顺序：

```text
main
 ├── HAL_Init
 ├── SystemClock_Config
 ├── MX_GPIO_Init
 ├── Platform_Init
 │    ├── 日志USART1绑定
 │    └── BootControl/I2C准备
 ├── PlatformBootControl_Read
 └── Bootloader_Run
```

正常无升级快速路径：

```text
BootControl = NONE
    ↓
不初始化SD/FatFs
    ↓
PlatformFlash_Init
    ↓
PlatformApplication_Validate
    ↓
PlatformApplication_Jump
```

升级路径：

```text
BootControl = UPDATE
    ↓
PlatformStorage_Init
    ↓
PlatformStorage_Mount
    ↓
检查升级包
    ↓
按需初始化PlatformFlash或PlatformTherapy
    ↓
安装并校验
    ↓
清理和复位
```

### PLAT-SEQ-001

BootControl为NONE时，不得初始化SDMMC或FatFs。

### PLAT-SEQ-002

启动APP前，QSPI必须进入正确的Memory-Mapped状态。

### PLAT-SEQ-003

Therapy USART2只在外部MCU升级时进入8E1 ROM Boot配置。

---

## 16. CMake设计

### 16.1 Platform源文件

```cmake
set(PLATFORM_STM32H7_SOURCES
    src/platform.c
    src/platform_storage.c
    src/platform_flash.c
    src/platform_boot_control.c
    src/platform_therapy.c
    src/platform_application.c
    src/platform_system.c
)

add_library(platform_stm32h7 STATIC
    ${PLATFORM_STM32H7_SOURCES}
)

target_include_directories(platform_stm32h7
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

    PRIVATE
        ${CMAKE_SOURCE_DIR}/FATFS/App
        ${CMAKE_SOURCE_DIR}/FATFS/Target
        ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FatFs/src
)

target_link_libraries(platform_stm32h7
    PUBLIC
        firmware_types

    PRIVATE
        board_bsp
        spi_nor
        at24
        stm32_rom_boot
        stm32cubemx
        firmware_logging
)
```

### CMAKE-001

`firmware_types`为PUBLIC依赖，因为Platform公共头文件暴露`firmware_status_t`及公共数据结构。

### CMAKE-002

`board_bsp`、`spi_nor`、`at24`、`stm32_rom_boot`和`stm32cubemx`为PRIVATE依赖，不得通过Platform公共头文件向上泄露。

### CMAKE-003

不得链接：

```text
firmware_storage_common
fw::segger_rtt
```

除非后续出现新的明确需求。

### CMAKE-004

`board_bsp`不得链接器件Drivers，Drivers也不得链接`board_bsp`。二者由`platform_stm32h7`组合。

### CMAKE-005

FatFs若已经包含在`stm32cubemx`目标中，不再额外建立重复的FatFs静态库。

### 16.2 上层依赖

```cmake
target_link_libraries(boot_services
    PUBLIC
        firmware_types
    PRIVATE
        platform_stm32h7
        firmware_crypto
)

target_link_libraries(boot_application
    PRIVATE
        boot_services
        platform_stm32h7
        firmware_logging
)
```

允许Boot Application直接链接Platform，用于BootControl、Watchdog、Reset和APP启动。

---

## 17. 禁止事项

Platform中禁止出现：

```text
Manifest JSON解析
UPDATE/CURRENT业务决策
版本比较策略
组件安装顺序
固件包签名策略
APP/GUI专用Flash写函数
HAL Handle公共类型
FatFs公共类型
动态内存分配
RTOS API
无限Busy等待
永久Watchdog刷新循环
```

禁止创建以下纯转发调用链：

```text
Service
  → Interface
  → Facade
  → Adapter
  → Platform
  → BSP
  → Driver
```

本项目最长的典型硬件调用链应保持为：

```text
Service → Platform → Driver → BSP回调 → HAL
```

或：

```text
Service → PlatformStorage → FatFs → sd_diskio → HAL_SD
```

---

## 18. 验收标准

### AC-001

Platform公共头文件中不出现任何STM32 HAL Handle和FatFs类型。

### AC-002

Platform、BSP和Drivers统一使用`firmware_status_t`，不存在重复错误码枚举。

### AC-003

工程中不存在`bsp_sd.c/h`，SD文件访问固定通过PlatformStorage、FatFs和`sd_diskio.c`完成。

### AC-004

PlatformStorage不存在多Backend配置、Facade或运行时多介质选择逻辑。

### AC-005

PlatformFlash同时组合`spi_nor`和`bsp_qspi_flash`，`spi_nor`不直接包含BSP头文件。

### AC-006

PlatformBootControl同时组合`at24`和`bsp_i2c`，`at24`不理解BootControl业务。

### AC-007

PlatformTherapy同时组合`stm32_rom_boot`、`bsp_isp_uart`和`bsp_isp_gpio`。

### AC-008

BootControl为NONE时，正常启动路径不初始化SDMMC和FatFs。

### AC-009

所有Flash Busy、EEPROM写周期、FatFs操作和ROM Boot ACK等待均存在有限超时或有限调用边界。

### AC-010

任何大文件安装均使用固定缓冲区流式执行，不使用动态内存，不一次性加载完整文件。

### AC-011

QSPI Runtime修改后和APP跳转前执行正确的Cache维护及Barrier。

### AC-012

SDMMC使用DMA时，`sd_diskio.c`正确处理STM32H7 D-Cache一致性。

### AC-013

日志使用USART1发送，日志失败不改变核心Boot流程。

### AC-014

Platform中不存在Manifest解析、Version Policy、升级顺序或Recovery决策。

### AC-015

CMake依赖不存在`board_bsp ↔ device_drivers`循环。

---

## 19. 最终原则

```text
Boot Application = 决定什么时候做什么
Services         = 完成一个完整升级业务操作
Platform         = 组合当前STM32H743的软件能力
BSP              = 当前PCB如何收发数据和控制引脚
Driver           = W25Q256、AT24、STM32 ROM Boot如何工作
```

核心关系：

> Platform同时组合BSP与Driver；BSP和Driver不是上下级。SD直接使用FatFs和`sd_diskio.c`，不建立BSP_SD。Application可以直接调用简单Platform能力，Services调用Platform完成复杂业务，不增加纯转发层。

