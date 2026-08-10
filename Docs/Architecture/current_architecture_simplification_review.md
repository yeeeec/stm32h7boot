# 当前项目软件架构简化审查

## 1. 审查结论

当前项目的主架构没有明显的整体性过度设计。`Application -> Services -> Interfaces`、`Adapters -> Platform/BSP/Driver` 的依赖方向清晰，`Composition` 也确实承担了静态对象和依赖关系组装的职责。

复杂度主要集中在三个位置：

1. 已经没有调用者的历史抽象仍然被编译进生产目标；
2. 同一个产品契约在 Manifest、Boot Control、Secondary MCU 和 Composition 中重复表达；
3. 顶层 Application 和 Composition 的内部状态/布线代码偏大，影响阅读，但这些代码承载的业务事务本身是真实的。

因此建议采用“先删死代码，再收敛契约，最后整理内部实现”的渐进方案，不建议把现有 Service 再拆成更多通用层，也不建议把所有接口合并成一个硬件服务。

## 实施状态

本次已落实 P0/P1 的可执行项，并完成不改变状态机语义的内部整理：

- 删除三个 Secondary MCU Composition getter 和 `Composition_IsInitialized()`；
- 删除无调用者的同步 `block_device_t`、同步 SPI NOR 转发以及 `watchdog_t`/STM32 Watchdog Adapter；
- 将 therapy 上限统一为 `UPDATE_THERAPY_IMAGE_MAX_SIZE`，Manifest、Boot Control、Secondary MCU、Application、Composition 和测试继续各自校验该共享常量；
- 让 Manifest 直接引用 `BOOT_APP_RUNTIME_SIZE` 与 `BOOT_GUI_RUNTIME_SIZE`；
- 将 `Composition_Init()` 拆分为私有初始化阶段，并将 Application 可变状态收敛到 `application_context_t`；
- Micro-ECC/Image Authenticator 保持 Host 测试专用，生产 `firmware_crypto` 仅链接 SHA-256。

下文第 4 和第 6 节保留审查时发现的问题，作为变更依据和后续验收记录。

审查依据包括：

- [`Composition/src/composition.c`](../../Composition/src/composition.c) 及 [`Composition/include/composition/composition.h`](../../Composition/include/composition/composition.h)
- [`Application/src/application.c`](../../Application/src/application.c) 及 [`Application/internal/application/application_config.h`](../../Application/internal/application/application_config.h)
- [`Services/CMakeLists.txt`](../../Services/CMakeLists.txt)、[`Adapters/CMakeLists.txt`](../../Adapters/CMakeLists.txt)
- [`Services/src/use_case/update_service.c`](../../Services/src/use_case/update_service.c)、[`Services/src/use_case/secondary_mcu_update_service.c`](../../Services/src/use_case/secondary_mcu_update_service.c)
- 现有 `Docs/Architecture`、`Docs/Bootloader` 和 Host 测试中的实际调用关系

## 2. 当前架构判断

```text
CubeMX/HAL/Driver
        ^
Platform/BSP
        ^
Adapters  ---->  Interfaces  <---- Services
                                  ^
                              Application
                                  ^
                             Composition Root
```

依赖方向的实际含义是：

- `Application` 负责启动、升级、恢复、提交和最终故障决策；
- `Services` 负责可独立测试的能力或完整长流程；
- `Interfaces` 只定义上层需要的能力合同；
- `Adapters` 把 FatFs、QSPI、EEPROM、ROM Bootloader 等具体实现转换为合同；
- `Composition` 负责创建静态实例、工作缓冲区和依赖注入；
- `Platform/BSP/Driver` 负责系统节拍、看门狗、硬件和生成代码边界。

这个方向与现有架构基线一致，未发现 Application 或 Services 直接包含 HAL/FatFs/Driver 类型的结构性越层问题。

## 3. 合理且应该保留的部分

### 3.1 分层和 Composition Root

`Composition_Init()` 统一完成 Adapter、Provider、Service 和 Application 的绑定，所有长期对象由静态存储期对象持有。这避免了 Service 自己创建硬件对象，也避免了 Service Locator 和动态内存，适合当前裸机升级流程。该职责应保留；需要改进的是函数内部的可读性，而不是删除 `Composition` 层。

### 3.2 异步 Block Device

[`Interfaces/include/firmware/async_block_device.h`](../../Interfaces/include/firmware/async_block_device.h) 不是无意义的泛化。Flash 擦除和编程需要跨多个主循环迭代，接口明确分离了提交、轮询和结果观察，支持 watchdog、超时和 Host Fake。当前生产升级链路由 `UpdateService`、`ActiveValidationService`、`LaunchService` 使用，应该保留。

### 3.3 Package Source 与 Image Source 的分工

`package_source_t` 负责挂载、打开、关闭和文件生命周期；`firmware_image_source_t` 负责已经打开文件的大小和随机读取。`PackageImageSourceAdapter` 只有一个生产实现，但它解决了真实的生命周期和职责隔离问题，使 `SecondaryMcuUpdateService` 不依赖 FatFs。该桥接应保留，不应为了减少文件数量而把 FatFs 类型传入 Service。

### 3.4 Boot Control、Hash 和校验边界

`boot_control_store_t` 把 EEPROM A/B 存储与记录格式隔离；`hash_provider_t` 让 Manifest、请求、Runtime 和外部 MCU 镜像可以使用同一 SHA-256 合同；CRC、JSON、版本策略等纯能力保持在 Services 内部也合理。它们都有实际生产调用或 Host Fake，不属于“只有一个实现所以应该删除”的抽象。

### 3.5 长流程 Service 模型

`UpdateService`、`SecondaryMcuUpdateService`、`ActiveValidationService` 和 `LaunchService` 的 `Start/Process/GetState/GetResult` 模型适合异步 Flash、串口 ROM 协议、增量 Hash 和 fail-closed 恢复。Service 内部有大量状态是事务本身的要求，不应简单改成一次调用完成，也不应再包一层“通用状态机 Service”。

其中：

- `UpdateService` 负责 APP/GUI Runtime 的验证、擦除、写入和回读；
- `SecondaryMcuUpdateService` 负责 therapy.app.bin 的第三方 MCU 串口升级和失败恢复；
- `BootControlService` 负责 Active Record 的持久化和原子提交；
- `ActiveValidationService` 负责当前 Runtime 完整性校验；
- `LaunchService` 负责 XIP、向量表检查和不可返回的跳转安全边界。

这些职责相互独立，当前不建议合并。

### 3.6 单实现接口中仍有必要的边界

`system_reset_t`、`system_clock_t`、`log_sink_t`、`application_jump_t`、`xip_controller_t` 和 `mcu_programmer_t` 目前以 STM32 实现为主，但它们分别对应系统副作用、时间/日志测试替身、启动安全边界、QSPI 模式控制和第三方 MCU 协议边界。Host 测试已经使用其中多个 Fake。判断接口是否必要应看替换边界和测试价值，而不是只看实现数量。

## 4. 审查时可以简化或删除的部分

下表按“当前仓库内无调用者”与“仍有职责但实现偏重”区分。

| 项目 | 证据 | 建议 | 优先级 |
|---|---|---|---|
| `Composition_GetSecondaryMcuUpdateService()` | 仅在声明和实现出现；Application 通过 `Application_Configure()` 直接注入 | 删除声明、实现和对应 README 描述 | 高 |
| `Composition_GetSecondaryMcuImageSource()` | 无调用者；Application 通过已打开的 `package_source_t` 使用桥接对象 | 删除声明、实现和对应 README 描述 | 高 |
| `Composition_GetSecondaryMcuProgrammer()` | 无调用者；Programmer 已在 Service 初始化时注入 | 删除声明、实现和对应 README 描述 | 高 |
| `Composition_IsInitialized()` | 生产代码无调用者，只有 Composition 注释/文档提及 | 若无诊断需求则删除；否则放入 internal/test API，不作为公共 Composition API | 高 |
| 同步 `block_device_t` | 仓库中除定义、Adapter 双接口和示例注释外没有调用者；生产链路全部使用异步接口 | 删除同步接口、`SpiNorBlockAdapter` 的同步转发和 `SpiNorBlockAdapter_Interface()`；先确认没有仓库外 ABI 使用者 | 高 |
| `watchdog_t` 与 `stm32_watchdog_adapter.*` | Adapter 未被 Composition 或其他生产模块初始化；Platform 已直接维护刷新周期 | 从 `firmware_adapters` 和生产接口中移除；看门狗继续作为 Platform 系统职责 | 高 |
| `image_authenticator_t` / Micro-ECC Authenticator | `firmware_crypto` 生产库只编译 SHA-256；Authenticator 只在 Host `crypto_json_test` 路径出现 | 在签名升级尚未落地前移出生产架构，保留到实验/安全特性目录或删除 | 中 |

删除上述模块不会减少当前升级能力，因为它们没有参与当前生产调用图；删除前应在 CI 中保留一次全仓搜索和完整构建检查，防止仓库外集成代码依赖这些符号。

## 5. 值得简化但不应直接删除的部分

### 5.1 `Composition_Init()` 的布线可拆成私有函数

当前 [`Composition/src/composition.c`](../../Composition/src/composition.c) 约 500 行，其中约 300 行是初始化和依赖填充。职责正确，但所有阶段混在一个函数里，修改一个硬件绑定时需要扫描整段代码。

建议只在 `.c` 内拆成少量私有函数，例如：

```text
InitLogging()
InitStorage()
InitPackageAccess()
InitCapabilityServices()
InitUpdateServices()
PublishApplicationDependencies()
```

这些函数不应新增公共接口、注册表或 Provider 容器；它们只是把现有顺序分组，并保留“全部成功后才发布 Application 依赖”的语义。

### 5.2 Application 的全局变量和技术阶段

[`Application/src/application.c`](../../Application/src/application.c) 约 900 行，包含 request load、therapy 文件 ownership、关闭重试、commit 和恢复等多个状态及标志。它们并非重复业务 Service，但目前以大量文件级全局变量表达，导致状态关系难以追踪。

建议：

1. 将这些变量收敛到一个私有 `application_context_t`；
2. 把 therapy open/start/process/close、unmount retry 等重复分支封装为 `.c` 内 helper；
3. 顶层状态名优先表达系统语义，具体 I/O 进度继续由 Service 保存。

不建议现在新增 `ApplicationUpdateService` 或 `TherapyOrchestratorService`。Application 已经是编排层，再增加一个只转发调用的 Service 会形成新的中间层。

### 5.3 `UpdateService` 的 APP/GUI 重复

[`Services/src/use_case/update_service.c`](../../Services/src/use_case/update_service.c) 约 1769 行，APP 和 GUI 各自拥有 source、erase、program、readback、hash 阶段。这里的规模主要来自真实的异步事务和错误恢复，不是单纯封装过度。

可以在 `.c` 内引入只读的 component descriptor，统一保存：

- 组件位和文件名；
- 目标 offset、容量和对应 stage；
- 成功后的 Active Record 字段；
- 失败错误码映射。

descriptor 只作为私有数据表，不能升级为新的公开“通用镜像框架”。这样可以减少 APP/GUI 分支重复，同时保留清晰的事务状态和可诊断阶段。

### 5.4 Manifest 与 Update Request 的重复校验

`ManifestService` 和 `UpdateRequestService` 都做 JSON member 校验、SHA-256 helper、package/component 解析，但两者 schema 和信任边界不同：一个验证包内 Manifest，一个验证受信请求并绑定 Manifest。不要合并成“大解析 Service”。

可做的收敛仅限于：把纯 JSON token 查找、整数/十六进制解析和固定 SHA helper 复用已有 `json_document` 或 `.c` 内部 helper；不要建立通用 schema framework。

### 5.5 单字段 dependencies 结构体

`manifest_service_dependencies_t` 和 `update_request_service_dependencies_t` 当前都只有一个 `hash` 指针，这是轻微的接口包装。可以将 `Init()` 改为直接接收 `const hash_provider_t *`，但项目其他 Service 已统一使用 dependencies struct。

建议暂不为了这一处追求形式上的简化；只有在下一次 API 兼容窗口统一调整所有 Service 初始化风格时再处理，优先级低。

## 6. 审查时的重复数据和配置来源

### 6.1 therapy 512 KiB 限制

当前同一约束出现在：

- `MANIFEST_THERAPY_MAX_SIZE`；
- `BOOT_CONTROL_THERAPY_MAX_SIZE`；
- `COMPOSITION_THERAPY_TARGET_CAPACITY` 及编译期检查；
- `SecondaryMcuUpdateService_Start()` 中的 `512UL * 1024UL`；
- Manifest、Boot Control、Application 测试中的边界断言。

这不是每一层都重新验证就是错误；Manifest、持久化读取和目标写入必须各自做边界检查。但数值应只有一个业务契约来源。

建议在 `Services/include/services/common` 增加一个明确命名的共享升级限制头，或把常量收敛到现有共享类型头；Manifest、Boot Control、Secondary MCU Service 和 Composition 只引用该常量。目标地址、擦除页起点/数量仍属于 Composition 的板级策略，不要下沉到通用 Service。

### 6.2 APP/GUI 布局和 Manifest 上限

`runtime_layout.h` 已经定义 APP/GUI offset、XIP base 和容量，但 `manifest_service.c` 仍有独立的 `APP_MAXIMUM_IMAGE_SIZE`、`GUI_MAXIMUM_IMAGE_SIZE`。这会产生“Manifest 允许的大小”和“实际可写分区大小”漂移的风险。

当前产品采用固定布局时，建议让 Manifest 直接使用 `BOOT_APP_RUNTIME_SIZE`/`BOOT_GUI_RUNTIME_SIZE`；若未来需要多板卡布局，则把一个只读 layout/limits 对象作为 Manifest 初始化依赖，并由 Composition 提供，不能继续保留两套数字。

### 6.3 Active Record 与 Manifest 的重复字段

Active Record 保存已安装镜像的 size、version 和 SHA-256 是必要的持久化快照，不应为了减少字段而删除。可优化的是统一字段命名和校验 helper，确保 component mask 决定哪些字段有效，避免 Application、Boot Control 和 Update Service 各自推断默认值。

## 7. 不建议的简化方向

- 不要把 `Application`、`UpdateService` 和 `SecondaryMcuUpdateService` 合并成一个“升级管理器”；这会把系统决策、Flash 事务和串口协议混在一起。
- 不要删除 `async_block_device_t` 并退回同步 Flash API；这会破坏长事务的看门狗和恢复边界。
- 不要为了“只有一个实现”删除 `xip_controller_t`、`application_jump_t` 或 `mcu_programmer_t`；它们是硬件副作用和安全边界，且已有 Host Fake/独立测试价值。
- 不要建立 Service Locator、动态注册表或通用 Provider 工厂；当前静态 Composition 已足够明确。
- 不要为了 APP/GUI/therapy 统一而建立跨组件通用升级框架；三者的目标介质、提交语义和失败恢复不同，私有 descriptor 已足够。

## 8. 推荐实施顺序

### P0：删除死 API 和孤立生产目标

1. 删除三个 Secondary MCU Composition getter；
2. 删除 `Composition_IsInitialized()`，或将其移到 internal/test；
3. 确认同步 `block_device_t` 无外部使用后删除同步分支；
4. 移除 `watchdog_t` Adapter；
5. 将未启用的 Image Authenticator 从生产架构中隔离。

每一步都执行全仓搜索、Host 测试和目标构建，避免只删除头文件而留下 CMake 或文档引用。

### P1：建立契约单一来源

1. 统一 therapy 最大镜像大小；
2. 统一 APP/GUI Manifest 上限与 Runtime layout；
3. 保留每层的防御性校验，但禁止复制裸数字；
4. 在 Bootloader contract 文档中标出唯一来源及各层检查点。

### P2：只整理内部实现

1. 拆分 `Composition_Init()` 私有初始化函数；
2. 收敛 Application 文件级状态到私有 context；
3. 用私有 component descriptor 减少 UpdateService 的 APP/GUI 重复；
4. 合并重复的纯 helper，不新增公共抽象。

## 9. 验收标准

- 生产 CMake target 不再编译无调用者的 Interface/Adapter；
- `rg` 能证明每个保留 Interface 至少有一个生产消费者或 Host Fake；
- therapy、APP、GUI 的大小限制各只有一个定义来源，其他位置仅引用；
- Application 仍只负责系统级编排和最终决策，Service 仍拥有长流程状态；
- `Composition` 仍是唯一依赖组装点，未引入 Service Locator 或动态内存；
- Host 测试覆盖单独 APP、GUI、therapy 以及组合请求，且 SHA-256 和失败恢复行为不变；
- 完整构建通过，`git diff --check` 无空白错误，文档中的文件链接有效。

## 10. 最终判断

项目当前不是“分层过多导致无法维护”的架构。应保留真实的异步设备、文件生命周期、持久化、Hash、XIP 和外部 MCU 协议边界；应删除的是没有调用者的同步/看门狗/未启用签名抽象和 Composition 访问器。其余复杂度主要来自升级事务本身，采用私有 helper、descriptor 和单一配置来源即可降低维护成本，不需要再引入新的公共层。

## 11. 实施验证（2026-08-11）

- `cmake --build build/Debug --parallel 4` 通过；Services、Adapters、Application、Composition 及最终 `stm32h7boot.elf` 均重新编译、链接成功。
- `git diff --check` 通过；生产代码和 CMake 中已不存在已删除的同步 `block_device_t`、watchdog Adapter、Composition getter 或 `Composition_IsInitialized()` 的引用。
- therapy 容量、APP/GUI Manifest 上限分别只定义在 `UPDATE_THERAPY_IMAGE_MAX_SIZE` 和 Runtime Layout；各层保留的均为对共享契约的防御性校验。
- `build/Debug` 不配置 CTest；Host 测试工程当前显示 `CMAKE_C_COMPILER-NOTFOUND`，因此本次无法在该工作站执行 Host 单元测试。测试目标及 APP、GUI、therapy 单独/组合场景仍已在 `Tests/Host` 中保留，待安装原生 C 编译器后应作为 CI 验收执行。

验证后的结论是：当前分层与异步事务边界合理，已删除无实际职责的中间抽象；不应继续以“统一升级框架”、Service Locator 或通用状态机为名新增层级。`UpdateService` 的 APP/GUI 内部重复可在未来以私有 helper 继续整理，但现阶段不值得引入公开 descriptor 或新的 Service。
