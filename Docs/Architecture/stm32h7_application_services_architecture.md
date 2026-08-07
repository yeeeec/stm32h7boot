# STM32H7 Application 与 Services 架构实施规范

> 状态：Architecture Baseline v1  
> 目标：指导人工和 Codex 判断代码归属、Service 形态、状态机粒度、对象所有权和依赖关系。  
> 本文不定义具体 Bootloader 发布包、安全协议、分区或升级业务规则。

---

## 1. 基本原则

```text
Application
    = 系统级编排与最终决策

Service
    = 一项能力/用例如何完成

Interface
    = 上层需要的稳定底层能力

Adapter
    = 具体技术如何满足 Interface

Composition
    = 谁与谁绑定
```

Application 不应因为 SD 换成 eMMC、QSPI Driver 换实现、FatFs 换文件系统而修改公共状态机语义。

---

## 2. Application 职责

Application 负责：

- 固件顶层状态机；
- 模式选择；
- Service 调用顺序；
- 跨 Service 的事务顺序；
- 系统级失败处理；
- 重试、恢复、复位、启动、故障的最终决策；
- 裸机主循环或 RTOS 任务级编排；
- 系统级事件；
- 何时进入/退出某一用例。

Application 不负责：

- Manifest/协议解析；
- 文件内容读取实现；
- Flash 擦写；
- SHA/CRC 算法；
- HAL/BSP/Driver 操作；
- 单一 Service 内部步骤；
- Adapter 创建；
- 技术错误细节。

判断原则：

> 如果逻辑回答“整个固件下一步做什么”，属于 Application。

---

## 3. Service 职责

Service 负责：

- 一项完整能力或用例；
- 能力内部状态机；
- 长流程进度；
- Interface 调用顺序；
- 技术不变量；
- 数据校验；
- 底层错误归一化；
- 稳定结果输出。

Service 不负责：

- 决定整个固件进入哪个模式；
- 创建 Adapter；
- 查找 HAL Handle；
- 修改 Application 状态；
- 用 callback 反向控制 Application；
- 决定多个独立 Use-case 之间的产品业务顺序。

判断原则：

> 如果逻辑回答“这项能力具体如何完成”，属于 Service。

---

## 4. 归属判定表

| 问题 | 归属 |
|---|---|
| 决定系统进入正常、升级、恢复、诊断或故障模式？ | Application |
| 协调两个独立 Service 的先后关系？ | Application |
| 一项可独立描述并测试的完整能力？ | Service |
| 某项能力内部的阶段/校验/重试？ | Service 内部 |
| 纯无状态算法或数据转换？ | Utility / Model |
| 定义上层所需底层能力？ | Interface |
| 适配 HAL/FatFs/BSP/Driver？ | Adapter |
| 创建实际对象并绑定？ | Composition |

---

## 5. Service 三种形态

### 5.1 无状态模块

适用：

- 版本比较；
- 边界验证；
- checked arithmetic；
- 格式转换；
- 纯策略函数。

形式：

```c
bool version_is_newer(
    const version_t *candidate,
    const version_t *current);
```

不要机械生成 `Init/Process/GetState`。

### 5.2 同步 Capability Service

适用：

- 短时间完成；
- 不需要跨主循环保存阶段；
- 无复杂异步等待。

示例：

```c
service_status_t boot_control_service_load(
    boot_control_service_t *service,
    boot_active_record_t *record);
```

### 5.3 长流程 Use-case Service

适用：

- 大文件；
- Flash 擦写；
- 大量 Hash；
- 多步骤事务；
- 异步设备；
- 可恢复长流程。

推荐生命周期：

```text
init
start
process
get_state
get_result
cancel (按需)
```

---

## 6. Capability 与 Use-case

可按项目规模组织：

```text
Services/
├── Capability/
└── UseCase/
```

Capability 示例：

```text
BootControl
Validation
Verification
Storage
Diagnostics
```

Use-case 示例：

```text
Update
Provisioning
Export
ExternalDeviceUpdate
```

允许：

```text
Use-case Service -> Capability Service -> Interfaces
```

禁止循环依赖。

模块数量少时允许平铺，不为目录而目录。

---

## 7. Application 状态机粒度

推荐状态表达系统语义：

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

不推荐把技术动作提升为 Application 状态：

```text
FILE_OPEN
FLASH_ERASE
FLASH_PROGRAM
HASH_CALCULATE
EEPROM_PAGE_WRITE
SDMMC_WAIT
```

这些属于 Service/Adapter。

判定：

> 如果更换底层技术后 Application 状态名必须改变，状态粒度通常过低。

---

## 8. Service 对象模型

有状态 Service 保存自己的阶段：

```c
struct update_service
{
    update_service_state_t state;
    update_stage_t stage;
    update_result_t result;

    uint32_t current_offset;

    const package_source_t *package_source;
    const async_block_device_t *storage;
    const hash_provider_t *hash;

    uint8_t *io_buffer;
    size_t io_buffer_size;
};
```

Service 拥有：

```text
当前阶段
当前偏移
操作结果
稳定错误
Interface 依赖引用
内部上下文
```

Application 只拥有：

```text
顶层系统状态
Service 引用
系统级事件
最终决策
```

不得在 Application 和 Service 同时保存同一内部流程阶段。

---

## 9. 增量执行

长流程 `process()` 必须有界。

一次调用建议最多执行：

- 一个状态转换；
- 一个擦除单元；
- 一个数据块读取/写入；
- 一次异步轮询；
- 一个 Hash 数据块。

不得：

```c
while (entire_file_not_done) {
    /* 在一次 process() 中处理整个文件 */
}
```

除非明确证明最大执行时间满足系统实时预算。

需要检查：

- 看门狗；
- 超时；
- 可恢复状态；
- 介质断开；
- 短读；
- Device busy；
- 异步完成。

---

## 10. Application 与 Service 交互

默认：

```text
Application -> Service_Start
Application -> Service_Process
Application -> Service_GetState
Application -> Service_GetResult
Application -> 决定下一顶层状态
```

不推荐 Service 通过深层 callback 直接修改 Application。

Callback 更适合：

```text
ISR -> Driver
HAL callback -> Adapter
DMA complete -> Adapter
```

---

## 11. 事件模型

Application 可以维护少量系统级事件：

```c
typedef enum
{
    APPLICATION_EVENT_NONE               = 0U,
    APPLICATION_EVENT_UPDATE_REQUESTED   = 1U << 0,
    APPLICATION_EVENT_RECOVERY_REQUESTED = 1U << 1,
    APPLICATION_EVENT_FATAL_ERROR        = 1U << 2
} application_event_flag_t;
```

事件不得携带 HAL Handle、FatFs 类型、Driver 指针。

技术事件必须由 Adapter/Service 吸收并转换。

---

## 12. 错误模型

逐层转换：

```text
HAL / Middleware native error
        ↓
Adapter technical error
        ↓
Service stable error
        ↓
Application system decision
```

示例：

```text
HAL_TIMEOUT
 -> ADAPTER_ERROR_TIMEOUT
 -> UPDATE_ERROR_TARGET_WRITE
 -> Application 决定 UPDATE/RECOVERY/FAULT
```

Service 结果建议：

```c
typedef struct
{
    service_status_t status;
    service_error_t error;
    service_stage_t stage;
    int32_t native_error;
} service_result_t;
```

`native_error` 只用于诊断，不直接驱动产品业务决策。

---

## 13. 对象生命周期

Composition 拥有：

- Application 根对象；
- Service 长期实例；
- Adapter 长期实例；
- 长期 buffer。

禁止：

- Service 保存栈上临时依赖；
- `malloc/free`；
- Service Locator；
- Service 自己创建 Adapter；
- Application访问 Composition 私有对象。

---

## 14. Composition

典型：

```c
static update_service_t g_update;
static boot_control_service_t g_boot_control;

static uint8_t g_manifest_buffer[16 * 1024];
static uint8_t g_io_buffer[4096];

firmware_status_t Composition_Init(void)
{
    /* 构造 Adapter Interface */
    /* 初始化 Capability / Use-case Service */
    /* 注入 Application */
    return FIRMWARE_STATUS_OK;
}
```

Composition 可以知道：

```text
开发构建 -> SD/FatFs Adapter
正式构建 -> eMMC filesystem Adapter
```

但 Application/Services API 不随之改变。

---

## 15. 业务规则隔离

本文中的 Update/Recovery 只是架构示例。

具体 Bootloader 是否存在：

```text
Active Record
Trusted Request
Runtime layout
Manifest
Recovery package
```

以及它们的具体规则，不由本文件定义。

唯一真相源：

```text
Bootloader/stm32h7_bootloader_business_rules.md
Bootloader/stm32h7_firmware_release_package_contract.md
Bootloader/stm32h7_bootloader_detailed_design.md
```

---

## 16. Codex 验收清单

- [ ] Application 不包含 HAL/FatFs/BSP/Driver 头文件。
- [ ] Service 不包含 HAL/FatFs/BSP/Driver 头文件。
- [ ] 技术状态没有提升成 Application 状态。
- [ ] 长流程由 Service 保存阶段。
- [ ] `process()` 有界。
- [ ] Service 不反向 callback 控制 Application。
- [ ] 依赖由 Composition 注入。
- [ ] 不使用动态内存。
- [ ] 错误逐层转换。
- [ ] 产品业务规则没有被复制到本架构文档。
