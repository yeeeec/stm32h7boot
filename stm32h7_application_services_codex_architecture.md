# STM32H7 Firmware Application 与 Services 架构实施规范

> 适用范围：STM32H7 Bootloader 固件与 Application 固件。  
> 本文只规定 Application 层、Services 层及其衔接方式，不定义具体产品业务、升级协议、分区地址或设备功能。  
> 本文用于指导 Codex 判断代码归属、生成模块骨架、建立依赖关系并执行架构验收。

---

## 1. 架构目标

Application 与 Services 的设计应满足：

1. Application 只负责固件级运行编排和顶层状态决策；
2. Services 提供边界明确、可复用、可测试的系统能力；
3. Application 不直接访问 Interfaces、Adapters、BSP、Platform、HAL 或 Middleware；
4. Services 只通过 Interfaces 使用底层能力；
5. 具体实现由 Composition Root 创建并注入；
6. 长流程采用有状态、增量执行模型；
7. 短操作保持同步 API，不机械地增加状态机；
8. 不使用动态内存；
9. 不使用全局字符串设备注册中心；
10. Bootloader 与 Application 固件采用相同的软件结构规则。

总体依赖关系：

```text
main.c
  |
  v
Composition Root
  |
  +------> Application
  |            |
  |            v
  |         Services
  |            |
  |            v
  |         Interfaces
  |            ^
  |            |
  +-------- Adapters
               |
               v
       Platform / BSP / Drivers / Middleware
```

编译期依赖必须保持：

```text
Application -> Services
Services    -> Interfaces
Adapters    -> Interfaces
Composition -> Application + Services + Adapters + Platform + BSP
```

严格禁止：

```text
Application -> Interfaces
Application -> Adapters
Application -> BSP / Platform / HAL / Middleware

Services -> Adapters
Services -> BSP / Platform / HAL / Middleware
Services -> CubeMX 生成头文件
```

---

## 2. Application 层定义

Application 是固件的最高层运行编排模块。

它回答：

```text
系统当前处于什么模式？
当前应该启动哪一个用例？
某个用例完成后，系统下一步进入什么状态？
某个用例失败后，系统采取重试、恢复、复位还是故障停机？
```

Application 负责：

- 固件顶层状态机；
- 工作模式选择；
- 系统级事件处理；
- Service 的启动顺序；
- Service 完成、失败或取消后的状态转换；
- 裸机周期调度或 RTOS 任务编排；
- 系统级恢复策略；
- 最终的启动、复位、停机或故障决策。

Application 不负责：

- 文件读写；
- Flash 擦写；
- Hash、CRC 或签名算法执行；
- Manifest 或协议解析；
- USB、UART、QSPI、I2C 等技术状态处理；
- BSP 设备控制；
- HAL Handle 访问；
- Service 内部步骤编排；
- Adapter 或 Driver 的创建和查找。

判断原则：

> 涉及整个固件运行模式或多个用例之间切换的逻辑，属于 Application。

---

## 3. Services 层定义

Services 提供固件所需的稳定能力。

它回答：

```text
某项能力如何完成？
执行到了哪一步？
执行结果是什么？
该能力内部需要维护哪些状态和不变量？
```

Services 负责：

- 封装一项完整能力或用例；
- 管理该能力内部执行步骤；
- 管理长流程内部状态；
- 通过 Interfaces 调用底层能力；
- 执行业务规则、顺序约束和数据校验；
- 将底层错误转换为稳定的 Service 错误；
- 向 Application 暴露状态、结果和进度；
- 保证接口调用顺序和模块不变量。

Services 不负责：

- 决定固件进入哪个顶层模式；
- 创建具体 Adapter；
- 从 BSP 查找设备；
- 访问 CubeMX 全局 Handle；
- 创建 RTOS 任务；
- 直接改变 Application 状态；
- 通过 Callback 反向控制 Application。

判断原则：

> 属于一项能力内部的执行步骤，属于 Service。

---

## 4. Application 与 Service 的边界判定

Codex 在新增代码前必须使用以下判定表。

| 问题 | 是 | 否 |
|---|---|---|
| 是否决定整个固件进入正常、升级、恢复、诊断或故障模式？ | Application | 继续判断 |
| 是否协调两个或多个独立 Service 的先后关系？ | Application 或 Use-case Service，按系统范围判断 | 继续判断 |
| 是否是一项完整、可独立描述的系统能力？ | Service | 继续判断 |
| 是否属于某项能力内部的步骤、校验或状态？ | Service 内部模块 | 继续判断 |
| 是否只是无状态算法或数据转换？ | Utility / Model，不创建 Service | 继续判断 |
| 是否直接适配 HAL、BSP、Driver 或 Middleware？ | Adapter | 继续判断 |
| 是否定义 Service 需要的底层能力？ | Interface | 不得放入 Application 或 Service 实现 |

典型判定：

```text
决定是否进入升级模式
    -> Application

执行完整升级过程
    -> Update Service

升级过程中的文件校验、擦除、写入、提交
    -> Update Service 内部阶段

SHA-256 算法
    -> Utility 或 Middleware

调用 FatFs 读取升级文件
    -> PackageSource Adapter

定义“读取升级文件”的抽象能力
    -> package_source Interface
```

---

## 5. Service 的三种形态

Codex 不得把所有模块都生成成 `Init + Process + GetState`。

应根据行为选择以下三种形态。

### 5.1 无状态模块

适用于：

- 版本比较；
- 数据格式校验；
- Checked arithmetic；
- 分区重叠检测；
- 编解码；
- 纯算法；
- 不依赖外部状态的规则判断。

推荐 API：

```c
bool version_is_compatible(
    const version_t *current,
    const version_t *required);

service_status_t partition_table_validate(
    const partition_table_t *table);

bool checked_u32_add(
    uint32_t lhs,
    uint32_t rhs,
    uint32_t *result);
```

不得生成：

```c
version_service_init();
version_service_process();
```

除非模块确实拥有长期状态或外部依赖。

### 5.2 同步 Capability Service

适用于：

- 单次调用能够在可接受时间内完成；
- 操作不需要等待异步事件；
- 不需要跨多轮主循环保存执行阶段；
- 失败后不需要复杂恢复。

推荐 API：

```c
typedef struct
{
    const boot_control_store_t *store;
} boot_control_service_t;

service_status_t boot_control_service_init(
    boot_control_service_t *service,
    const boot_control_store_t *store);

service_status_t boot_control_service_load(
    boot_control_service_t *service,
    boot_control_record_t *record);

service_status_t boot_control_service_commit(
    boot_control_service_t *service,
    const boot_control_record_t *record);
```

不要求提供 `process()`。

### 5.3 异步或长流程 Use-case Service

适用于：

- 大文件处理；
- Flash 擦写；
- USB、网络或串口等待；
- 多步骤事务；
- 可取消操作；
- 需要掉电检查点；
- 单次执行可能阻塞主循环或看门狗。

统一生命周期建议为：

```text
init
start
process
get_state
get_result
cancel（按需）
```

推荐 API：

```c
typedef enum
{
    UPDATE_SERVICE_IDLE = 0,
    UPDATE_SERVICE_RUNNING,
    UPDATE_SERVICE_SUCCEEDED,
    UPDATE_SERVICE_FAILED,
    UPDATE_SERVICE_CANCELLED
} update_service_state_t;

service_status_t update_service_init(
    update_service_t *service,
    const update_service_dependencies_t *dependencies,
    const update_service_config_t *config);

service_status_t update_service_start(
    update_service_t *service,
    const update_request_t *request);

void update_service_process(
    update_service_t *service);

update_service_state_t update_service_get_state(
    const update_service_t *service);

const update_result_t *update_service_get_result(
    const update_service_t *service);

service_status_t update_service_cancel(
    update_service_t *service);
```

---

## 6. Services 内部组织

中等项目建议在逻辑上区分：

```text
Services/
├── Capability/
└── UseCase/
```

### Capability Service

提供较小、可复用的能力，例如：

```text
BootControl
Verification
Storage
Configuration
Diagnostics
TimePolicy
```

特点：

- 职责单一；
- 可被多个 Use-case Service 使用；
- 一般为同步或小型状态对象；
- 不知道整个固件当前模式。

### Use-case Service

封装一项完整用例，例如：

```text
Update
Recovery
Provisioning
Export
ExternalDeviceUpdate
```

特点：

- 向 Application 暴露完整用例；
- 可以组合 Capability Service；
- 可以拥有内部状态机；
- 隐藏技术步骤；
- 返回稳定的状态和结果。

允许依赖：

```text
Use-case Service -> Capability Service -> Interfaces
```

禁止：

```text
Capability Service -> Use-case Service
Service A -> Service B -> Service A
```

当 Service 数量较少时，不必立即创建 `Capability/UseCase` 两级目录，可以平铺；当模块数量超过约 5 个或职责开始混淆时再分类。

---

## 7. Application 状态机设计

Application 状态必须表达系统语义，而不是技术动作。

推荐状态粒度：

```text
STARTUP
SELECT_MODE
NORMAL
UPDATE
RECOVERY
DIAGNOSTIC
LAUNCH
FAULT
SHUTDOWN
```

不推荐：

```text
USB_ENUMERATING
FILE_OPENING
FLASH_ERASING
FLASH_PROGRAMMING
HASH_CALCULATING
EEPROM_WRITING
```

这些状态应属于 Service 或 Adapter。

判定规则：

> 如果底层从 USB 更换为 eMMC、从 QSPI 更换为其他存储后，Application 状态名称必须修改，则该状态粒度过低。

Application 状态机应保持“薄”：

```c
static void application_handle_update(
    application_t *application)
{
    update_service_process(
        application->services.update);

    switch (update_service_get_state(
                application->services.update)) {
    case UPDATE_SERVICE_RUNNING:
        break;

    case UPDATE_SERVICE_SUCCEEDED:
        application_transition_to(
            application,
            APPLICATION_STATE_LAUNCH);
        break;

    case UPDATE_SERVICE_FAILED:
        application->last_error =
            update_service_get_result(
                application->services.update)->error;

        application_transition_to(
            application,
            APPLICATION_STATE_RECOVERY);
        break;

    default:
        application_transition_to(
            application,
            APPLICATION_STATE_FAULT);
        break;
    }
}
```

Application 不得重新展开 Service 内部步骤：

```c
/* 禁止 */
package_mount();
manifest_parse();
image_verify();
flash_erase();
flash_write();
control_record_commit();
```

---

## 8. Application 对象模型

建议 Application 使用一个根对象，不通过大量全局变量保存状态。

```c
typedef struct
{
    update_service_t *update;
    boot_service_t *boot;
    recovery_service_t *recovery;
    diagnostic_service_t *diagnostic;
} application_services_t;

typedef struct
{
    application_services_t services;

    application_state_t state;
    application_state_t previous_state;

    application_event_flags_t pending_events;
    application_error_t last_error;

    bool initialized;
} application_t;
```

初始化依赖：

```c
typedef struct
{
    update_service_t *update;
    boot_service_t *boot;
    recovery_service_t *recovery;
    diagnostic_service_t *diagnostic;
} application_dependencies_t;

application_status_t application_init(
    application_t *application,
    const application_dependencies_t *dependencies);
```

周期入口：

```c
void application_process(
    application_t *application);
```

可以保留一个供 `main.c` 调用的薄门面：

```c
void Application_Init(void);
void Application_Process(void);
```

门面只转发到 Composition 创建的 `application_t` 实例，不包含业务实现。

---

## 9. Service 对象模型

有状态 Service 应保存自己的运行状态，不得将内部状态放到 Application。

```c
struct update_service
{
    update_service_state_t state;
    update_stage_t stage;

    update_request_t request;
    update_result_t result;

    uint16_t current_item;
    uint32_t current_offset;

    const package_source_t *package_source;
    const component_registry_t *installers;
    const image_verifier_t *verifier;
    const boot_control_store_t *control_store;
    const boot_watchdog_t *watchdog;

    uint8_t *io_buffer;
    size_t io_buffer_size;
};
```

Service 拥有：

```text
当前执行阶段
当前数据偏移
内部上下文
内部错误
操作结果
Interface 依赖
```

Application 只拥有：

```text
顶层系统状态
Service 引用
系统级事件
最终结果摘要
系统恢复决策
```

不得让 Application 与 Service 同时保存同一流程的执行阶段。

---

## 10. 增量执行规则

长流程 Service 的 `process()` 必须有界。

每次调用建议只执行：

- 一个状态转换；
- 一个擦除单元；
- 一个数据块读写；
- 一次短时校验步骤；
- 一次异步状态检查。

示例：

```c
void update_service_process(
    update_service_t *service)
{
    if (service == NULL ||
        service->state != UPDATE_SERVICE_RUNNING) {
        return;
    }

    switch (service->stage) {
    case UPDATE_STAGE_PREPARE:
        update_process_prepare(service);
        break;

    case UPDATE_STAGE_VALIDATE:
        update_process_validate_chunk(service);
        break;

    case UPDATE_STAGE_ERASE:
        update_process_one_erase_unit(service);
        break;

    case UPDATE_STAGE_WRITE:
        update_process_one_data_chunk(service);
        break;

    case UPDATE_STAGE_VERIFY:
        update_process_one_verify_chunk(service);
        break;

    case UPDATE_STAGE_COMMIT:
        update_process_commit(service);
        break;

    default:
        update_fail(
            service,
            UPDATE_ERROR_INTERNAL);
        break;
    }
}
```

Codex 必须检查：

- 单次 `process()` 是否可能长时间阻塞；
- 是否存在无超时等待；
- 是否在循环中一次处理整个文件；
- 是否需要看门狗刷新；
- 是否可以在下一轮继续；
- 中断或介质断开后是否有确定状态。

---

## 11. Application 与 Service 的交互方式

裸机架构优先采用：

```text
Application 调用 Service_Process
Application 查询 Service_State
Application 根据 Service_Result 决策
```

推荐：

```c
update_service_process(application->services.update);

if (update_service_get_state(
        application->services.update) ==
        UPDATE_SERVICE_FAILED) {
    application_transition_to(
        application,
        APPLICATION_STATE_RECOVERY);
}
```

不推荐 Service Callback 直接修改 Application：

```c
/* 禁止作为默认方式 */
update_service_set_complete_callback(
    application_on_update_complete);
```

Callback 主要用于：

```text
ISR -> Driver
HAL callback -> Adapter
DMA completion -> Adapter
Middleware event -> Adapter
```

Service 与 Application 之间默认使用状态查询或事件消息，不使用深层回调控制流。

---

## 12. 事件模型

Application 可维护少量系统级事件：

```c
typedef enum
{
    APPLICATION_EVENT_NONE             = 0U,
    APPLICATION_EVENT_UPDATE_REQUESTED = 1U << 0,
    APPLICATION_EVENT_RECOVERY_REQUESTED = 1U << 1,
    APPLICATION_EVENT_SHUTDOWN_REQUESTED = 1U << 2,
    APPLICATION_EVENT_FATAL_ERROR      = 1U << 3
} application_event_flag_t;
```

事件规则：

1. 事件只表达系统级语义；
2. HAL、Driver 或 Adapter 事件不得直接进入 Application；
3. 技术事件先由 Adapter/Service 吸收并转换；
4. Application 在自己的 `process()` 中消费事件；
5. ISR 只能设置原子标志或写入固定容量队列；
6. Application 事件不得携带 HAL Handle、FatFs 类型或 Driver 指针。

---

## 13. 错误模型

错误应逐层转换。

```text
HAL / Middleware 原生错误
        ↓
Adapter 技术错误
        ↓
Service 稳定错误
        ↓
Application 系统决策
```

示例：

```text
HAL_TIMEOUT
    -> ADAPTER_ERROR_TIMEOUT
    -> UPDATE_ERROR_TARGET_WRITE
    -> Application 进入 RECOVERY
```

Service 对外结果建议为：

```c
typedef struct
{
    service_status_t status;
    service_error_t error;
    service_stage_t stage;
    int32_t native_error;
} service_result_t;
```

Application 可以读取：

```text
status
稳定错误
失败阶段
```

Application 不得按以下错误做系统决策：

```text
HAL_TIMEOUT
FR_DISK_ERR
USBH_FAIL
QSPI 状态寄存器值
```

`native_error` 只用于日志和诊断。

---

## 14. 初始化与对象所有权

推荐初始化顺序：

```text
1. CubeMX Generated Init
2. Platform_Init
3. BSP_Init
4. Composition_Init
5. Application_Init
6. 进入主循环或启动 RTOS
```

对象所有权：

```text
Composition
    拥有 Application、Service、Adapter 的长期实例
    拥有长期 Buffer
    负责依赖注入

Application
    拥有顶层状态

Service
    拥有自身运行状态

Adapter
    拥有底层技术状态

Driver
    拥有器件状态

调用者
    拥有请求对象，或 Service 在 start 时复制请求
```

所有依赖对象必须在使用者整个生命周期内保持有效。

禁止：

- 保存栈上临时依赖地址；
- Service 内部调用 `malloc/free`；
- Service 自行创建 Adapter；
- Service 从全局 Service Locator 查找实现；
- Application 直接访问 Composition 内部对象。

---

## 15. Composition Root 衔接

Composition Root 是具体对象绑定的唯一位置。

```c
static application_t g_application;
static update_service_t g_update_service;
static boot_service_t g_boot_service;

static usb_package_source_t g_usb_source;
static qspi_installer_t g_qspi_installer;

static uint8_t g_update_io_buffer[4096];

composition_status_t Composition_Init(void)
{
    composition_status_t status;

    status = usb_package_source_construct(
        &g_usb_source,
        BSP_GetUsbStorage());

    if (status != COMPOSITION_OK) {
        return status;
    }

    status = qspi_installer_construct(
        &g_qspi_installer,
        BSP_GetExternalFlash());

    if (status != COMPOSITION_OK) {
        return status;
    }

    update_service_dependencies_t update_dependencies = {
        .package_source =
            usb_package_source_interface(&g_usb_source),
        .installers =
            component_registry_get(),
        .verifier =
            image_verifier_get(),
        .control_store =
            boot_control_store_get(),
        .watchdog =
            boot_watchdog_get()
    };

    update_service_config_t update_config = {
        .io_buffer = g_update_io_buffer,
        .io_buffer_size = sizeof(g_update_io_buffer)
    };

    status = update_service_init(
        &g_update_service,
        &update_dependencies,
        &update_config);

    if (status != COMPOSITION_OK) {
        return status;
    }

    application_dependencies_t application_dependencies = {
        .update = &g_update_service,
        .boot = &g_boot_service,
        .recovery = &g_recovery_service,
        .diagnostic = &g_diagnostic_service
    };

    return application_init(
        &g_application,
        &application_dependencies);
}
```

Composition 可以知道所有层，但不得包含：

- Application 状态转换；
- Service 业务流程；
- 文件读写；
- Flash 擦写；
- 协议解析。

---

## 16. 推荐目录

初期精简结构：

```text
Application/
├── CMakeLists.txt
├── application.c
├── application.h
└── application_state.h

Services/
├── CMakeLists.txt
├── service_status.h
├── service_error.h
├── update_service.c
├── update_service.h
├── boot_service.c
├── boot_service.h
├── recovery_service.c
└── recovery_service.h

Composition/
├── CMakeLists.txt
├── composition.c
└── composition.h
```

模块增多后的结构：

```text
Application/
├── Include/
│   └── application.h
├── Private/
│   ├── application_context.h
│   └── application_internal.h
└── Source/
    ├── application.c
    ├── application_transition.c
    └── application_events.c

Services/
├── Common/
│   ├── service_status.h
│   ├── service_error.h
│   └── service_result.h
│
├── Capability/
│   ├── BootControl/
│   ├── Verification/
│   ├── Storage/
│   └── Diagnostics/
│
└── UseCase/
    ├── Update/
    ├── Recovery/
    └── Provisioning/

Composition/
├── composition.c
└── composition.h
```

Codex 不得在第一阶段创建大量无内容目录。只有出现实际模块时才创建对应目录。

---

## 17. CMake 依赖

建议 Target：

```text
firmware_application
firmware_services
firmware_composition
```

依赖：

```cmake
target_link_libraries(firmware_application
    PUBLIC
        firmware_services
)

target_link_libraries(firmware_services
    PUBLIC
        firmware_interfaces
)

target_link_libraries(firmware_composition
    PRIVATE
        firmware_application
        firmware_services
        firmware_adapters
        platform_stm32h7
        board_bsp
)
```

Application 和 Services 不得获得以下 include path：

```text
Core/Inc
Drivers/STM32H7xx_HAL_Driver/Inc
Middlewares/Third_Party/FatFs
USB_HOST
FATFS
BSP
Platform
Adapters
```

Codex 必须使用目标级：

```cmake
target_sources()
target_include_directories()
target_link_libraries()
target_compile_definitions()
```

禁止使用全局：

```cmake
include_directories()
link_libraries()
add_definitions()
file(GLOB_RECURSE ...)
```

---

## 18. Codex 自动判定流程

Codex 在生成或修改 Application/Service 代码前必须按以下顺序执行。

### 步骤 1：识别行为范围

回答：

```text
该逻辑是否决定整个固件的工作模式？
该逻辑是否只属于一项能力内部？
该逻辑是否只是纯算法？
该逻辑是否直接适配底层技术？
```

归类为：

```text
Application
Service
Utility / Model
Adapter
Interface
```

### 步骤 2：判断 Service 形态

回答：

```text
是否有长期状态？
是否需要跨多轮主循环继续？
是否等待异步事件？
是否处理大文件或 Flash？
是否需要取消、超时、恢复或进度？
```

选择：

```text
无状态函数模块
同步 Capability Service
异步 Use-case Service
```

### 步骤 3：定义公共 API

公共 API 必须：

- 使用项目稳定类型；
- 不包含 HAL、FatFs、USB、Driver 类型；
- 明确对象、请求、结果和生命周期；
- 明确错误返回；
- 对长流程提供状态查询；
- 不暴露内部阶段，除非用于诊断；
- 不返回内部可变对象供上层修改。

### 步骤 4：定义依赖

列出 Service 所需能力：

```text
时钟
存储
文件源
校验
日志
看门狗
控制记录
目标安装
```

若底层能力尚无 Interface，先创建 Interface，再创建 Adapter。

不得在 Service 中直接包含底层头文件。

### 步骤 5：确定对象所有权

Codex 必须明确：

```text
谁创建对象？
谁拥有 Buffer？
谁保存状态？
谁负责初始化？
谁负责调用 process？
谁读取结果？
```

默认：

```text
Composition 创建
Service 保存内部状态
Application 调用和决策
```

### 步骤 6：实现最小骨架

先生成：

1. 公共头文件；
2. 数据结构；
3. `init()`；
4. 状态枚举；
5. API 参数校验；
6. 最小状态转换；
7. CMake Target；
8. 单元测试 Fake。

不得一次生成所有具体业务实现。

### 步骤 7：执行架构检查

编译并检查：

- Application 是否错误包含底层头文件；
- Service 是否获得 HAL/FatFs include path；
- Service 是否直接调用 BSP；
- Application 是否展开 Service 内部步骤；
- 是否存在循环依赖；
- 是否存在无界 `process()`；
- 是否存在动态内存；
- 是否存在隐藏全局状态。

---

## 19. Codex 生成代码的强制规则

Codex 必须：

1. 在修改前读取现有目录、CMake Target 和公共头文件；
2. 优先复用现有 Service、Interface 和错误类型；
3. 保持 Application 薄；
4. 保持 Service API 面向能力，而不是面向 HAL；
5. 使用显式依赖注入；
6. 对所有指针参数做检查；
7. 对所有状态转换进行合法性检查；
8. 对长流程采用有界 `process()`；
9. 使用固定容量 Buffer；
10. 为可选依赖提供 Null 实现或显式 `NULL` 策略；
11. 先生成最小可编译骨架；
12. 每一阶段确保 Host 或 Target 编译通过；
13. 在无法从现有工程确定行为时添加 `TODO`，不得猜测业务规则。

Codex 不得：

1. 在 `main.c` 中实现业务状态机；
2. 在 Application 中包含 `stm32h7xx_hal.h`、`ff.h`、`usb_host.h`；
3. 在 Service 中调用 `HAL_*`、`BSP_*` 或 `MX_*`；
4. 创建万能 `system_service`、`device_manager` 或全局 Service Locator；
5. 让 Service Callback 直接修改 Application 状态；
6. 将所有模块机械地生成为异步 Service；
7. 在 Application 和 Service 中重复保存同一流程状态；
8. 使用动态内存；
9. 创建字符串查找式注册中心；
10. 为尚不存在的模块批量创建空目录和空文件。

---

## 20. 自动验收清单

Codex 完成代码生成后必须逐项确认：

### Application

- [ ] Application 只包含 Service 公共头文件；
- [ ] Application 状态表达系统语义；
- [ ] Application 不含 USB、文件、Flash、DMA 等技术步骤；
- [ ] Application 不直接访问 Interface；
- [ ] Application 不直接访问 Composition 的具体实现；
- [ ] Application 根据 Service 状态和结果做决策；
- [ ] Application `process()` 单次执行时间有界。

### Services

- [ ] 每个 Service 有单一明确职责；
- [ ] Service 形态与执行特征匹配；
- [ ] 长流程有 `start/process/get_state/get_result`；
- [ ] 短操作未被无意义地状态机化；
- [ ] Service 只依赖 Interfaces 或更低层 Capability Service；
- [ ] Service 不访问 HAL、BSP、Adapter、Middleware；
- [ ] Service 内部状态不泄漏给 Application 修改；
- [ ] Service 错误已转换为稳定错误；
- [ ] 无 Service 循环依赖。

### Composition

- [ ] 所有具体实现只在 Composition 中绑定；
- [ ] 对象和 Buffer 生命周期明确；
- [ ] 初始化顺序明确；
- [ ] 初始化失败可以向上返回；
- [ ] Composition 不包含业务状态转换。

### Build

- [ ] Application Target 只链接 Services；
- [ ] Services Target 只链接 Interfaces；
- [ ] Application/Services 无 HAL 和 Middleware include path；
- [ ] 未使用全局 include path；
- [ ] Host Test 可以替换 Interface 为 Fake；
- [ ] 新增代码在 CubeMX 再生成后不会丢失。

---

## 21. 最终代码评审准则

所有 Application 与 Service 代码都应通过以下两句判断：

> Application 不应该知道一项能力如何完成。

> Service 不应该决定整个固件下一步进入什么运行模式。

最终职责关系：

```text
Application
    管理固件顶层状态
    启动和观察 Use-case Service
    根据结果执行系统决策

Use-case Service
    封装完整操作流程
    管理长流程内部状态
    组合 Capability Service 或 Interfaces

Capability Service
    提供较小且可复用的能力
    封装稳定规则和数据处理

Interfaces
    定义底层能力需求

Composition
    创建对象、注入依赖、建立注册表
```

该模型应同时适用于 Bootloader 与 Application 固件，不依赖具体业务内容，也不依赖裸机或 RTOS 调度方式。
