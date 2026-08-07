# STM32H7 Bootloader 与 Application 通用软件架构

> 状态：Architecture Baseline v1  
> 范围：STM32H7 Bootloader 与 Application 固件的软件结构、依赖方向、CubeMX 衔接和构建组织。  
> 非范围：升级业务状态机、发布包格式、分区地址、安全协议、回滚策略。

---

## 1. 架构目标

本架构必须同时满足：

1. CubeMX 可以持续参与代码生成；
2. Bootloader 与 Application 使用一致的软件分层原则；
3. Application 与 Services 不直接依赖 HAL、FatFs、CubeMX Handle 或具体器件；
4. 具体硬件与 Middleware 差异通过 Adapter/BSP/Platform 吸收；
5. 依赖通过 Composition Root 静态注入；
6. 不依赖动态内存；
7. CMake Target 和 include-path 在编译期约束依赖方向；
8. 长流程可以增量运行并保持看门狗可维护；
9. Host Fake 能替代硬件 Adapter 进行单元测试；
10. CubeMX 生成代码与产品 Application 层概念严格区分。

---

## 2. 核心分层

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

另设：

```text
Composition Root
```

Composition Root 不属于业务层，负责创建长期对象并绑定依赖。

编译期依赖必须保持：

```text
Application -> Services + Interfaces
Services    -> Interfaces + Shared Types
Adapters    -> Interfaces + Platform/BSP/Drivers/Middleware
Composition -> Application + Services + Adapters + Platform + BSP
```

严格禁止：

```text
Application -> Adapters / BSP / Platform / HAL / Middleware
Services    -> Adapters / BSP / Platform / HAL / Middleware
Services    -> CubeMX Generated headers
```

---

## 3. CubeMX Generated Integration

CubeMX 生成目录中的 `App`、`main.c`、`MX_*_Init()` 不是产品架构中的 Application 层。

统一定义为：

> **Generated Integration Layer**

典型内容：

```text
Core/
├── Src/main.c
├── Src/stm32h7xx_it.c
├── Src/system_stm32h7xx.c
├── Src/gpio.c
├── Src/quadspi.c
├── Src/sdmmc.c
├── Src/i2c.c
├── Src/ltdc.c
└── ...

FATFS/App
FATFS/Target
USB_HOST/App      (若启用)
USB_HOST/Target   (若启用)
```

职责：

- HAL/CMSIS 系统入口；
- 时钟、MPU、Cache 和 MCU 外设初始化；
- HAL Handle 实例化；
- DMA/NVIC/GPIO Alternate Function；
- CubeMX Middleware Glue；
- IRQ/HAL callback 入口。

禁止承担：

- 产品状态机；
- 升级策略；
- 文件业务语义；
- Service 编排；
- 外部器件业务策略。

---

## 4. `main.c` 定位

`main.c` 是：

> **CubeMX-owned firmware startup shell**

推荐逻辑：

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

若 CubeMX 当前直接生成全部 `MX_*_Init()` 调用，可以保留；架构上仍视为 Generated Integration。

`main.c` 不应：

- 包含业务状态机；
- 直接调用升级 Service 的技术步骤；
- 解析文件；
- 写外部 Flash；
- 决定版本策略。

---

## 5. MCU 外设初始化组织

外设初始化应独立：

```text
gpio.c/h
quadspi.c/h
sdmmc.c/h
i2c.c/h
ltdc.c/h
dma2d.c/h
tim.c/h
usart.c/h
...
```

每个文件只负责：

- 对应 HAL Handle；
- `MX_*_Init()`；
- 与该外设直接相关的 DMA/NVIC 配置。

`main.h` 不应成为项目公共 API，不允许 Application/Services 包含。

---

## 6. Platform

Platform 表示：

> 与 STM32H7、Cortex-M7、编译器和运行环境相关，但与当前 PCB 外部器件连接无关的系统能力。

建议：

```text
Platform/STM32H7/
├── platform.c
├── platform_time.c
├── platform_reset.c
├── platform_critical.c
├── platform_interrupt.c
├── platform_cache.c
├── platform_memory.c
├── platform_watchdog.c
└── platform_reset_reason.c
```

Platform 可以依赖：

- CMSIS；
- STM32 HAL；
- CubeMX Handle；
- compiler intrinsic。

Platform 不负责：

- 外部 W25Q256/EEPROM/LCD 器件协议；
- 文件系统业务；
- 产品升级状态机；
- Application 策略。

---

## 7. BSP

BSP 表示：

> 当前 PCB 的器件实例、连接、电源、复位、片选和板级差异。

建议：

```text
BSP/
├── bsp.c
├── bsp_board.c
├── bsp_external_flash.c
├── bsp_eeprom.c
├── bsp_display.c
└── ...
```

BSP 可以私有依赖 CubeMX Handle，但其公共头文件不得向上暴露 HAL 类型。

BSP 不重复 `MX_*_Init()`；它在 MCU 外设已经初始化后完成板级外部器件绑定和安全状态。

---

## 8. Device Driver

Device Driver 表示具体外部芯片的通用协议实现，例如：

```text
Drivers/Devices/
├── W25Q256/
├── AT24C128/
└── ...
```

Driver 负责：

- 命令；
- 寄存器；
- 器件状态；
- read/program/erase；
- 器件时序和容量规则。

Driver 不知道：

- 产品分区用途；
- Application 状态；
- HAL Handle 名称；
- 当前 PCB GPIO。

推荐：

```text
Driver
  -> Driver Port
      <- BSP / Platform binding
```

---

## 9. Middleware

Middleware 包括：

- FatFs；
- USB Host；
- 网络栈；
- 图形/文件系统等第三方组件。

Middleware 原生类型只能出现在：

- Generated Integration；
- Adapter；
- 必要的 BSP/Platform 私有实现。

不得出现在 Application/Services/Interfaces 公共 API 中。

---

## 10. Interfaces

Interfaces 从上层能力需求出发，不复制 HAL/FatFs API。

推荐目录：

```text
Interfaces/
├── Storage/
├── Update/
├── Verification/
├── System/
├── Communication/
└── Diagnostics/
```

接口要求：

- 显式 `context`；
- 固定宽度类型；
- 明确同步/异步语义；
- 明确对象和 buffer 生命周期；
- 稳定错误码；
- 可用 Host Fake 实现；
- 不包含具体平台头文件。

示例：

```c
typedef struct
{
    void *context;
    firmware_status_t (*read)(
        void *context,
        uint32_t offset,
        uint8_t *data,
        uint32_t size);
} read_device_t;
```

---

## 11. Adapters

Adapter 把具体技术实现转换为 Interface。

典型关系：

```text
package_source_t       <- FatFs SD/eMMC Package Adapter
update_request_store_t <- FatFs Request Store Adapter
async_block_device_t   <- W25Q256 Adapter
system_reset_t         <- STM32H7 Reset Adapter
xip_controller_t       <- STM32H7 QSPI XIP Adapter
application_jump_t     <- Cortex-M Jump Adapter
```

Adapter 可以依赖：

- Interface；
- Platform；
- BSP；
- Driver；
- Middleware；
- Generated Integration。

Adapter 不依赖：

- Application；
- 上层业务状态机；
- 具体 Service 实现。

介质从 SD 切换到 eMMC 时，只允许修改 Composition/Adapter，不应改变上层公共语义。

---

## 12. Composition Root

Composition Root 是唯一允许知道具体实现类型的位置。

职责：

1. 创建静态 Application/Service/Adapter 对象；
2. 创建长期 buffer；
3. 初始化对象；
4. 将 Interface 实例注入 Service/Application；
5. 决定开发构建使用 SD Adapter 还是正式构建使用 eMMC Adapter；
6. 保证依赖对象生命周期覆盖使用者。

禁止：

- 写业务状态转换；
- 解析 Manifest；
- 擦写 Flash；
- 执行版本策略；
- 把 Service 内部步骤展开到 Composition。

---

## 13. 中断与 Callback

ISR/HAL callback 只允许：

- 设置原子状态；
- 设置事件标志；
- 推入固定容量队列；
- 通知 Adapter；
- 记录轻量状态。

禁止：

- 文件系统操作；
- Flash 长流程；
- Application 状态机；
- Manifest 解析；
- 阻塞等待。

推荐路径：

```text
IRQ
 -> HAL IRQ Handler
 -> HAL callback
 -> Adapter event
 -> Service observes event
```

---

## 14. 构建边界

建议 CMake Targets：

```text
firmware_generated
firmware_platform
firmware_bsp
firmware_device_drivers
firmware_interfaces
firmware_adapters
firmware_services
firmware_application
firmware_composition
```

Application 与 Services target 不得获得：

```text
Core/Inc
Drivers/STM32H7xx_HAL_Driver/Inc
FATFS/Target
USB_HOST/Target
具体 Devices Driver include path
```

除非通过明确的基础类型 Target 且不暴露技术实现。

---

## 15. 规则所有权

本文件只拥有“架构规则”。

以下内容必须引用其他文档，不在本文件重复定义：

```text
Bootloader 升级条件
Request 信任模型
Manifest Schema
APP/GUI 文件布局
QSPI Runtime 地址
EEPROM Active Record 格式
Recovery/掉电业务
版本策略
```

具体见：

```text
Bootloader/stm32h7_bootloader_business_rules.md
Bootloader/stm32h7_firmware_release_package_contract.md
Bootloader/stm32h7_bootloader_detailed_design.md
```

---

## 16. Codex 架构验收

Codex 每次新增模块必须检查：

- [ ] Application 是否直接包含 HAL/FatFs/BSP？
- [ ] Service 是否直接包含 HAL/FatFs/BSP/Driver？
- [ ] Adapter 是否只向上实现 Interface？
- [ ] CubeMX 生成文件是否只承担集成职责？
- [ ] Composition 是否只装配、不实现业务？
- [ ] 长期对象是否静态分配？
- [ ] 是否存在 Service Locator/字符串设备注册中心？
- [ ] CMake include path 是否破坏分层？
- [ ] 业务规则是否被错误写入通用架构层？
