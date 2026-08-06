# STM32H7 Bootloader 与 Application 通用软件架构设计

> 本文只讨论软件架构、代码所有权、模块边界、依赖方向和构建组织。  
> 不讨论 Bootloader 或 Application 的具体业务状态机、升级策略、协议格式、分区地址和设备功能实现。  
> 当前 Bootloader 工程使用 SD 卡 + SDIO/SDMMC + FatFs；未来 eMMC 通过 Adapter 替换，不改变本文依赖方向。

---

## 1. 设计目标

本架构同时适用于：

- STM32H7 Bootloader 固件；
- STM32H7 Application 固件；
- 裸机主循环；
- 后续引入 RTOS 的 Application；
- CubeMX 持续参与代码生成的工程。

主要目标：

1. 保留 CubeMX 对芯片、时钟、外设、Middleware Glue 和中断入口的生成能力；
3. Bootloader 与 Application 使用相同的软件分层模型，但拥有各自独立的 CubeMX 生成代码；
4. Application 和 Services 不依赖 HAL、CubeMX、FatFs、USB Host 或具体外部芯片；
5. 外部器件驱动、Middleware 和板级代码可以复用；
6. 通过 CMake Target 和 include-path 隔离在编译期约束依赖；
7. 使用静态对象和显式依赖注入，不依赖动态内存或全局字符串注册中心。

---

## 2. 核心架构结论

推荐的逻辑分层为：

```text
Application
    ↓
Services
    ↓
Interfaces
    ↑
Adapters
    ↓
Platform / BSP
    ↓
Device Drivers / Middleware
    ↓
CubeMX Generated Integration
    ↓
HAL / LL / CMSIS
```

需要额外设置一个不属于业务层的装配模块：

```text
Composition Root
```

它负责创建对象、绑定实现、注册多实现组件并确定初始化顺序。

完整关系：

```text
CubeMX main.c
    ↓
Generated System / Peripheral Initialization
    ↓
Platform_Init
    ↓
BSP_Init
    ↓
Composition_Init
    ↓
Application_Init
    ↓
Application_Process / RTOS Start
```

---

## 3. CubeMX 生成代码不是 Application 层

CubeMX 目录中的 `App`、`main.c` 和各种 `MX_*_Init()` 不等于产品软件架构中的 Application。

例如：

```text
USB_HOST/App/
FATFS/App/
```

这里的 `App` 表示 CubeMX Middleware 的实例化和集成代码，更接近：

```text
Generated Middleware Integration / Glue
```

而不是：

```text
Product Application Layer
```

同样，`Core/Src/main.c` 是固件启动入口和初始化编排文件，不是业务应用层。

---

## 4. CubeMX Generated Integration 边界

建议把所有 CubeMX 生成内容在架构上统一定义为：

> **Generated Integration Layer**

这一层包含：

```text
启动和链接
├── startup_stm32h743xx.s
├── STM32H743xx_FLASH.ld
└── system_stm32h7xx.c

系统入口
├── main.c
└── main.h

外设实例初始化
├── gpio.c/h
├── fmc.c/h
├── crc.c/h
├── dma2d.c/h
├── ltdc.c/h
├── quadspi.c/h
├── i2c.c/h
├── tim.c/h
├── usart.c/h
├── sdmmc.c/h
└── iwdg.c/h

中断集成
├── stm32h7xx_it.c
└── stm32h7xx_it.h

Middleware 集成
├── FATFS/App
├── FATFS/Target
├── USB_HOST/App（可选）
└── USB_HOST/Target（可选）
```

Generated Integration 的职责是：

- 配置 HAL Handle；
- 配置 MCU 外设寄存器参数；
- 配置时钟、GPIO Alternate Function、DMA 和 NVIC；
- 提供中断入口和 HAL 回调入口；
- 实例化 CubeMX Middleware；
- 为项目代码提供已初始化的 MCU 外设实例。

它不负责：

- 板级外部器件初始化；
- 业务流程；
- 服务编排；
- 文件业务语义；
- 组件安装策略；
- 产品状态机。

---

## 5. `main.c` 的最终定位

`main.c` 应保持为：

> **CubeMX-owned firmware startup shell**

它只负责：

1. Cortex-M、HAL、系统时钟和外设初始化；
2. 调用项目各层初始化入口；
3. 驱动必须由主循环持续调用的 CubeMX Middleware；
4. 调用 Application 周期入口，或启动 RTOS 调度器。

推荐形态：

```c
int main(void)
{
    Generated_System_Init();
    Generated_Peripherals_Init();

    Platform_Init();
    BSP_Init();
    Composition_Init();
    Application_Init();

    for (;;) {
        Generated_Middleware_Process();
        Platform_Process();
        Application_Process();
    }
}
```

其中：

```text
Generated_System_Init
    = MPU、Cache、HAL、系统时钟、公共外设时钟

Generated_Peripherals_Init
    = 按确定顺序调用各个 MX_*_Init

Generated_Middleware_Process
    = USB Host、网络栈等需要轮询的生成级入口；SDIO/SDMMC + FatFs 通常不要求独立业务轮询
```

这些函数属于 Generated Integration，不属于 Platform 或 BSP。

如果当前 CubeMX 仍直接在 `main.c` 中生成全部 `MX_*_Init()` 调用，可以暂时保留；架构上仍应把它们视为 Generated Integration 的初始化序列。后续可以通过项目自有且生成安全的 Facade 收敛这些调用，但不应把它们转移到 Platform 或 BSP。

---

## 6. 外设初始化不应集中在 `main.c/h`

每个 MCU 外设应拥有独立的初始化单元：

```text
gpio.c/h
fmc.c/h
quadspi.c/h
ltdc.c/h
usart.c/h
...
```

每个单元只负责对应 MCU 外设实例：

```text
Handle 定义
MX_*_Init
参数配置
必要的 DMA Handle 关联
```

例如：

```text
quadspi.c/h
    负责 hqspi 和 MX_QUADSPI_Init

usart.c/h
    负责 huart1 和 MX_USART1_UART_Init

ltdc.c/h
    负责 hltdc 和 MX_LTDC_Init
```

`main.c` 只负责编排调用顺序，不保存各外设的实现逻辑。

`main.h` 只作为 CubeMX 生成边界的一部分存在。它不应成为项目各层公共头文件，也不应被 Services 或 Application 包含。

---

---

## 8. 中断文件的职责

`stm32h7xx_it.c` 属于 Generated Integration，负责：

- Cortex 异常入口；
- MCU IRQ Handler；
- 调用对应 HAL IRQ Handler；
- 向项目自有 Adapter 转发必要事件。

推荐：

```c
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
```

HAL callback 或项目事件转发应尽量放入独立 Adapter：

```text
Adapters/Interrupts/
├── uart_event_adapter.c
├── qspi_event_adapter.c
└── timer_event_adapter.c
```

ISR 和 HAL callback 中只允许：

- 更新原子状态；
- 设置事件标志；
- 写入环形缓冲区；
- 投递轻量消息；
- 调用非阻塞的底层事件转发函数。

禁止在中断中：

- 运行 Application 状态机；
- 访问文件系统；
- 执行 Flash 擦写流程；
- 解析协议或配置文件；
- 执行长时间阻塞调用。

---

## 9. Platform 层

Platform 表示：

> 与 STM32H7、Cortex-M7、编译器或运行环境有关，但与当前 PCB 上具体器件连接无关的系统能力。

推荐内容：

```text
Platform/STM32H7/
├── platform.c/h
├── platform_time.c/h
├── platform_reset.c/h
├── platform_critical.c/h
├── platform_interrupt.c/h
├── platform_cache.c/h
├── platform_memory.c/h
├── platform_watchdog.c/h
└── platform_reset_reason.c/h
```

Platform 可以依赖：

- CMSIS；
- STM32 HAL；
- CubeMX 生成的 MCU Handle；
- 编译器 intrinsic。

Platform 不负责：

- 调用 `MX_*_Init()`；
- 初始化 W25Q256、LCD、EEPROM 等外部器件；
- 挂载 FatFs；
- USB 文件业务；
- Application 流程。

Platform 应优先提供小而稳定的函数或窄接口，不建立全局万能 `platform_api_t`。

---

## 10. BSP 层

BSP 表示：

> 当前 PCB 的器件实例、连接关系、电源关系、复位关系和板级状态。

推荐内容：

```text
BSP/
├── bsp.c/h
├── bsp_board.c/h
├── bsp_led.c/h
├── bsp_button.c/h
├── bsp_display.c/h
├── bsp_external_flash.c/h
├── bsp_eeprom.c/h
└── bsp_target_device.c/h
```

BSP 知道：

- 外部器件连接到哪个 MCU 外设实例；
- 使用哪个 GPIO；
- 使用哪个 HAL Handle；
- 当前板卡设备实例数量；
- 外部器件复位、电源和使能顺序；
- 板卡版本差异。

BSP 可以私有包含：

```c
#include "main.h"
#include "gpio.h"
#include "quadspi.h"
#include "i2c.h"
```

但这些头文件不得通过 BSP 公共接口继续向上传播。

`BSP_Init()` 不重复调用 `MX_GPIO_Init()`、`MX_QUADSPI_Init()` 等 Generated 初始化，而是在 MCU 外设已经可用后初始化板级器件和安全状态。

---

## 11. Device Drivers 层

Device Driver 表示具体外部芯片或标准器件的通用驱动。

推荐放入：

```text
Drivers/
├── CMSIS/
├── STM32H7xx_HAL_Driver/
└── Devices/
    ├── W25Q256/
    ├── EEPROM/
    ├── LCD_Controller/
    └── Other_Device/
```

外部器件 Driver 负责：

- 芯片命令；
- 寄存器定义；
- 状态转换；
- 擦除、写入、读取等器件能力；
- 器件参数和时序约束。

Driver 不应知道：

- 当前产品业务；
- 分区用途；
- Application 状态；
- 具体 HAL Handle 名称；
- 当前 PCB GPIO 宏。

推荐采用 Port 注入：

```text
Device Driver
    ↓ depends on
Driver Port Interface
    ↑ implemented by
BSP / Platform Binding
    ↓
CubeMX Handle / HAL
```

这种一对一静态注入比全局 Driver Registry 更适合 MCU 固件。

---

## 12. Middleware 层

Middleware 保存 ST 或第三方通用组件：

```text
Middlewares/
├── ST/
│   └── STM32_USB_Host_Library/
└── Third_Party/
    ├── FatFs/
    ├── FreeRTOS/
    ├── littlefs/
    ├── mbedTLS/
    └── JSON_Parser/
```

Middleware 不包含产品业务代码。

CubeMX 生成的：

```text
FATFS/App
FATFS/Target
USB_HOST/App
USB_HOST/Target
```

属于 Middleware 与当前固件之间的 Generated Glue，不建议为了减少目录而强制搬入 `Middlewares`，否则会增加 CubeMX 再生成冲突。

项目自己的 Middleware Adapter 应放在 `Adapters`，不放入第三方源码目录。

---

## 13. Interfaces 层

Interfaces 定义 Services 所需的稳定能力契约。

接口应从上层需求出发，而不是直接复制 HAL、FatFs 或 Device Driver API。

推荐：

```text
Interfaces/
├── Types/
├── Storage/
├── Update/
├── Communication/
├── System/
├── Verification/
└── Diagnostics/
```

接口头文件不得包含：

```text
stm32h7xx_hal.h
main.h
ff.h
usb_host.h
quadspi.h
具体外部芯片头文件
```

接口应采用：

- 固定宽度类型；
- 显式 context；
- 稳定错误码；
- 明确所有权；
- 无动态内存要求；
- 可在 Host 上实现 Fake。

---

## 14. Adapters 层

Adapter 负责把具体底层能力转换为 Interfaces。

典型关系：

```text
Interface                         Adapter
-----------------------------------------------------
Storage Interface                 FatFs Storage Adapter
Package Source Interface          FatFs SD/eMMC Package Adapter
Trusted Request Store Interface   Trusted FatFs Request Store Adapter
Block Device Interface            External Flash Adapter
Clock Interface                   STM32H7 Clock Adapter
Reset Interface                   STM32H7 Reset Adapter
Logger Interface                  UART Logger Adapter
Communication Interface           USB/UART Adapter
```

Adapter 可以依赖：

- Interfaces；
- Platform；
- BSP；
- Device Drivers；
- Middleware；
- Generated Glue。

Adapter 不应依赖：

- Application；
- 具体业务状态机；
- 上层 Service 实现。

Adapter 是吸收底层技术细节的主要位置。

当前 Bootloader 的具体绑定示例：

```text
package_source_t       <- FatFs SD Package Source Adapter
update_request_store_t <- Trusted FatFs Request Store Adapter
block_device_t         <- W25Q256 Adapter

未来恢复 eMMC 后：
package_source_t       <- eMMC 文件系统 Adapter
update_request_store_t <- 具备受信写入保证的 eMMC Request Store Adapter
```

介质替换不得影响 Application、Services 或 Interfaces 的公共语义。

---

## 14.1 可信升级请求的架构边界

本项目把发布身份认证与 Bootloader 安装完整性拆分：

```text
Application Upgrade Preparation Service
    -> ECDSA Manifest Verification
    -> 创建绑定 manifest_sha256 的可信请求

Bootloader Update Service
    -> 校验可信请求
    -> SHA256(Manifest/APP/GUI)
    -> 安装与提交
```

规则：

- Manifest 验签属于正式 Application 的 Service，不属于 Bootloader Application 顶层状态机；
- Bootloader 不链接公钥、micro-ecc 或 Manifest Signature Verifier；
- `update_request_store_t` 是安全边界接口，不能被视为普通任意文件读取接口；
- Adapter 只实现技术访问，但正式 Composition 只能绑定满足受信写入保证的实现；
- 当前可移除 SD 卡人工请求是开发 trust override，必须通过构建配置与量产实现区分；
- 介质从 SD 切换到 eMMC 时，Application/Services 公共语义不变，但 Request Store 的安全属性必须重新验收。

---

## 15. Services 层

Services 表示可复用的系统能力和业务无关或弱业务相关的功能模块。

本文不规定具体 Service 内容，只规定边界：

- Service 只依赖 Interfaces 和基础类型；
- Service 不包含 HAL、CMSIS、FatFs、USB Host、BSP 或 Device Driver 头文件；
- Service 不自行查找或创建底层对象；
- Service 的所有外部依赖由 Composition 显式注入；
- Service 可以在 Host 上使用 Fake Adapter 测试。

Service API 可以采用：

```c
typedef struct service service_t;

typedef struct
{
    const dependency_a_t *dependency_a;
    const dependency_b_t *dependency_b;
} service_dependencies_t;

status_t service_init(
    service_t *service,
    const service_dependencies_t *dependencies);
```

---

## 16. Application 层

Application 是固件最上层的运行编排。

对于裸机工程：

```text
Application_Init
Application_Process
```

对于 RTOS 工程：

```text
Application_Init
Application_CreateTasks
osKernelStart
```

本文不定义 Application 的具体业务内容，只规定：

- Application 调用 Services；
- Application 不调用 HAL、BSP、FatFs 或具体 Driver；
- Application 不负责构造底层实现；
- Application 不访问 CubeMX Handle；
- Application 不包含生成级外设初始化；
- Application 的状态机、任务或控制器只使用 Service API。

---

## 17. Composition Root

Composition Root 是唯一允许同时知道以下模块的位置：

```text
Application
Services
Interfaces
Adapters
Platform
BSP
具体编译期开关
```

推荐目录：

```text
Composition/
├── composition.c/h
├── service_bootstrap.c/h
└── adapter_registry.c/h
```

职责：

1. 创建所有静态对象；
2. 构造 Driver Port；
3. 构造 Adapter；
4. 注册确实存在多实现的组件；
5. 将 Interface 注入 Service；
6. 确定 Service 初始化顺序；
7. 向 Application 提供已初始化的 Service 集合。

Composition 不包含业务流程。

推荐注册策略：

```text
固定一对一依赖
    → 构造时直接注入

同一接口存在多个实现且需要运行时选择
    → 固定容量静态注册表

ISR、DMA 或异步事件
    → 单监听者 Callback 或事件队列

固定 Platform/BSP 能力
    → 普通函数或显式对象，不做字符串注册
```

禁止使用全局字符串 Service Locator：

```c
driver_find("qspi0");
service_get("storage");
```

---

## 18. 必要的层间衔接

### 18.1 `main.c → Generated Integration`

机制：普通函数调用。

```text
Generated_System_Init
Generated_Peripherals_Init
Generated_Middleware_Process
```

### 18.2 `main.c → Platform/BSP/Composition/Application`

机制：固定初始化入口。

```text
Platform_Init
BSP_Init
Composition_Init
Application_Init
```

### 18.3 `Application → Services`

机制：直接调用 Service API。

不使用全局底层注册中心。

### 18.4 `Services → Interfaces`

机制：构造时依赖注入。

Service 只保存 Interface 指针或对象引用。

### 18.5 `Adapters → Platform/BSP/Middleware`

机制：直接链接具体底层模块，并向上实现 Interface。

### 18.6 `BSP → Device Drivers`

机制：设备对象构造和 Port 注入。

### 18.7 `Device Drivers → HAL`

机制：Driver Port，由 BSP 或 Platform Binding 实现。

### 18.8 `IRQ/HAL Callback → Adapter`

机制：轻量事件转发、标志或静态回调。

---

## 19. Bootloader 与 Application 的工程关系

Bootloader 和 Application 是两个独立固件镜像，应分别拥有：

- `.ioc`；
- `main.c`；
- startup；
- linker script；
- 中断向量表；
- Generated Integration；
- Application；
- Services；
- Adapters；
- Composition；
- 最终 ELF Target。

推荐仓库结构：

```text
Firmware/
├── Bootloader/
│   ├── stm32h7boot.ioc
│   ├── Core/
│   ├── FATFS/
│   ├── Application/
│   ├── Services/
│   ├── Interfaces/
│   ├── Adapters/
│   ├── Composition/
│   └── CMakeLists.txt
│
├── Application/
│   ├── stm32h7app.ioc
│   ├── Core/
│   ├── Generated Middleware Glue/
│   ├── Application/
│   ├── Services/
│   ├── Interfaces/
│   ├── Adapters/
│   ├── Composition/
│   └── CMakeLists.txt
│
├── Platform/
│   └── STM32H7/
├── BSP/
│   └── BoardName/
├── Drivers/
│   └── Devices/
├── Middlewares/
├── Shared/
└── cmake/
```

是否共享 Platform、BSP、Driver 和 Middleware，应由实际外设配置和版本一致性决定。

不应直接共享：

- 生成的 `main.c`；
- `stm32h7xx_it.c`；
- startup；
- linker script；
- CubeMX Handle 定义；
- 固件专属 Composition。

可以共享：

- 无 HAL 依赖的 Interface；
- 外部器件 Driver；
- Platform 通用实现；
- BSP 中可复用的板卡设备模块；
- 通用 Middleware；
- 固件间明确约定的数据契约。

---

## 20. 推荐目录模板

单个固件镜像内部推荐：

```text
stm32h7_firmware/
├── Application/
│   ├── CMakeLists.txt
│   └── ...
├── Services/
│   ├── CMakeLists.txt
│   └── ...
├── Interfaces/
│   ├── CMakeLists.txt
│   └── ...
├── Adapters/
│   ├── CMakeLists.txt
│   └── ...
├── Composition/
│   ├── CMakeLists.txt
│   └── ...
├── Platform/
│   └── STM32H7/
├── BSP/
├── Core/
│   ├── Inc/
│   └── Src/
├── Drivers/
│   ├── CMSIS/
│   ├── STM32H7xx_HAL_Driver/
│   └── Devices/
├── Middlewares/
│   ├── ST/
│   └── Third_Party/
├── FATFS/
├── USB_HOST/（可选）
├── Tests/
├── cmake/
├── CMakeLists.txt
├── CMakePresets.json
├── firmware.ioc
├── linker.ld
└── startup.s
```

现有 CubeMX 目录先保持不动。架构解耦优先通过代码所有权、Target 和 include path 实现，而不是先搬动所有生成文件。

---

## 21. CMake Target 映射

推荐 Target：

```text
cubemx_generated_<firmware>     OBJECT 或 STATIC
platform_stm32h7                STATIC
board_bsp                       STATIC
driver_<device>                 STATIC
middleware_<name>               STATIC
firmware_interfaces             INTERFACE
firmware_services               STATIC
firmware_adapters               STATIC
firmware_application            STATIC
firmware_composition            OBJECT 或 STATIC
<firmware>.elf                  EXECUTABLE
```

依赖方向：

```text
firmware_application
    → firmware_services

firmware_services
    → firmware_interfaces

firmware_adapters
    → firmware_interfaces
    → platform_stm32h7 / board_bsp / middleware / drivers

board_bsp
    → drivers / cubemx_generated

platform_stm32h7
    → cubemx_generated

firmware_composition
    → application / services / adapters / platform / bsp
```

必须避免全局：

```cmake
include_directories(...)
add_definitions(...)
file(GLOB_RECURSE ...)
```

Services Target 不应获得：

```text
Core/Inc
HAL include path
FatFs include path
SDIO/SDMMC、USB Host include path
BSP include path
Device Driver include path
```

---

## 22. 初始化顺序

推荐固定顺序：

```text
1. Reset Handler / C Runtime
2. Generated System Init
3. Generated Peripheral Init
4. Platform_Init
5. BSP_Init
6. Composition_Init
7. Application_Init
8. Middleware Process + Application Process
```

规则：

- Platform 不反向调用 Generated Peripheral Init；
- BSP 不调用 `MX_*_Init()`；
- Service 不调用 BSP Init；
- Application 不构造 Service；
- Composition 不执行 Application 业务；
- 初始化失败必须在当前层转换为稳定错误并向上返回。

---

## 23. 依赖禁止规则

严格禁止：

```text
Application → HAL / CubeMX / BSP / Driver / Middleware
Services    → HAL / CubeMX / BSP / Driver / Middleware
Interfaces  → HAL / CubeMX / BSP / Driver / Middleware
Driver      → Application / Services
BSP         → Application / Services
Platform    → Application / Services
Middleware  → Application / Services
Generated   → Application / Services
```

允许：

```text
Application → Services
Services → Interfaces
Adapters → Interfaces
Adapters → Platform / BSP / Drivers / Middleware / Generated Glue
BSP → Drivers / Generated
Platform → HAL / CMSIS / Generated Handles
Drivers → Driver Port
Composition → 所有需要装配的项目模块
```

---

## 24. 实施原则

在现有 CubeMX 项目上实施时，建议按以下顺序：

### 阶段 1：定义 Generated 边界

- 保持 CubeMX 目录不动；
- 明确 `main.c/h`、IRQ、SDMMC/SDIO、FATFS 和可选 USB_HOST 的代码所有权；
- 禁止业务逻辑进入生成文件；
- 保留 USER CODE 区域作为接入点。

### 阶段 2：外设初始化单元化

- 确保每个 MCU 外设由独立 `.c/.h` 管理；
- `main.c` 只保留初始化顺序；
- 外设 Handle 不向 Services/Application 暴露。

### 阶段 3：建立 Platform 和 BSP

- 提取系统级能力到 Platform；
- 提取板级器件状态和连接到 BSP；
- 不重复调用 Generated `MX_*_Init()`。

### 阶段 4：建立 Interfaces、Adapters 和 Services

- Services 只依赖 Interfaces；
- Adapter 吸收 HAL、Middleware 和 BSP 细节；
- 通过 CMake include path 验证边界。

### 阶段 5：建立 Composition

- 所有静态对象集中创建；
- 统一依赖注入；
- 只有多实现场景采用静态注册表；
- Application 只获得已装配的 Service API。

---

## 25. 架构验收标准

完成后应满足：

- CubeMX 再生成不会覆盖项目业务逻辑；
- `main.c` 不包含业务状态机；
- `main.h` 不被 Application 或 Services 包含；
- 外设初始化不集中实现于 `main.c/h`；
- 每个外设 Handle 只在 Generated、Platform、BSP 或 Adapter 内可见；
- Platform 不调用 `MX_*_Init()`；
- BSP 不重复初始化 MCU 外设；
- Device Driver 不依赖具体 HAL Handle；
- Middleware 第三方源码中没有项目业务代码；
- Services 可以脱离 STM32 HAL 在 Host 上编译；
- Application 只依赖 Services；
- Composition 是具体实现绑定的唯一位置；
- Bootloader 和 Application 拥有独立 Generated Integration；
- CMake Target 能在编译期阻止 Services 包含 HAL 或 FatFs；
- 不存在全局字符串 Driver/Service Locator；
- 注册只用于多实现选择和异步事件转发。

---

## 26. 最终架构摘要

```text
┌──────────────────────────────────────────────┐
│ CubeMX main.c                                │
│ Generated 初始化编排和运行入口               │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Composition Root                             │
│ 静态对象创建、依赖注入、实现注册             │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Application                                  │
│ 固件顶层运行编排                             │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Services                                     │
│ 稳定系统能力                                 │
└──────────────────────┬───────────────────────┘
                       │ depends on
┌──────────────────────▼───────────────────────┐
│ Interfaces                                   │
│ 与硬件和中间件无关的能力契约                 │
└──────────────────────▲───────────────────────┘
                       │ implemented by
┌──────────────────────┴───────────────────────┐
│ Adapters                                     │
│ 将底层技术实现转换为 Interface               │
└───────────────┬───────────────────┬──────────┘
                │                   │
┌───────────────▼────────────┐ ┌────▼──────────┐
│ BSP / Device Drivers       │ │ Platform      │
│ 板卡和外部器件             │ │ STM32H7 系统能力│
└───────────────┬────────────┘ └────┬──────────┘
                │                   │
┌───────────────▼───────────────────▼──────────┐
│ Middleware / CubeMX Generated Integration    │
│ 外设实例、IRQ、Middleware Glue          │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ HAL / LL / CMSIS / STM32H7 Hardware          │
└──────────────────────────────────────────────┘
```

最核心的实现原则是：

```text
CubeMX 负责生成和初始化 MCU 技术资源；
Platform 和 BSP 负责解释这些资源；
Adapters 把具体资源转换为稳定接口；
Services 只依赖稳定接口；
Application 只编排 Services；
Composition 负责把所有实现连接起来。
```
