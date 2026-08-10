# Composition 依赖装配说明

## 1. Composition 是什么

`Composition` 是固件的 Composition Root，也就是依赖装配中心。

它不负责决定“是否升级”或“是否启动”，也不直接实现 Flash 擦写、向量表校验和
处理器跳转。它只负责完成以下工作：

1. 创建并持有各个 Adapter、Service 和工作缓冲区。
2. 把具体的 BSP/Platform 实现转换为通用接口。
3. 将这些接口注入对应的 Service。
4. 将所有 Service 和接口组成 `Application` 所需的依赖图。

Composition 在启动时执行一次，成功后不再重复装配。实现位于
[`src/composition.c`](src/composition.c)，公共入口位于
[`include/composition/composition.h`](include/composition/composition.h)。

## 2. 启动调用顺序

固件启动顺序由 `Core/Src/main.c` 驱动：

```text
HAL/CubeMX 外设初始化
        |
        v
Platform_Init()
        |
        v
BSP_Init()
        |
        v
Composition_Init()
        |
        v
Application_Init()
        |
        v
while (1)
    Platform_Process()
    Application_Process()
```

`Composition_Init()` 必须在 `Platform_Init()` 和 `BSP_Init()` 成功后调用，因为它会
读取 BSP 提供的设备对象，例如外部 Flash、EEPROM 和 QSPI。任何初始化失败都直接
返回错误；Composition 不执行回滚，也不会把 `composition_initialized` 置为真。

## 3. 各层的职责

```text
Application       顶层策略：更新、校验、提交、复位、启动
    |
Services          领域能力：Update、Validation、Launch、Boot Control
    |
Interfaces        稳定契约：块设备、XIP、跳转、复位、Hash 等
    ^
Adapters          接口转换：把 STM32/HAL/BSP API 包装成 Interfaces
    |
BSP / Platform    板级设备和芯片相关操作
```

`Services` 只依赖 `Interfaces`，不应该包含具体 Adapter 的头文件。`Application` 只
驱动 Service 生命周期和策略。`Composition` 是唯一可以同时包含这些具体类型、并把
它们连接起来的模块。

## 4. 依赖注入是怎么发生的

以启动 APP 为例，调用关系如下：

```text
CortexMApplicationJumpAdapter
        |
        | CortexMApplicationJumpAdapter_Interface()
        v
application_jump_t
        |
        | 写入 launch_service_dependencies_t.application_jump
        v
LaunchService_Init()
        |
        v
LaunchService_Execute()
        |
        v
application_jump_t.execute(context, vector_table_address)
```

`Composition` 中先初始化具体 Adapter：

```c
CortexMApplicationJumpAdapter_Init(&jump_adapter);
```

然后取出 Adapter 对外提供的通用接口，注入 Launch Service：

```c
launch_dependencies.application_jump =
    CortexMApplicationJumpAdapter_Interface(&jump_adapter);

LaunchService_Init(&launch_service, &launch_dependencies);
```

Launch Service 只认识 `application_jump_t`，不认识
`cortex_m_application_jump_adapter_t`。因此将来更换跳转实现时，只需要提供相同的
`application_jump_t`，并修改 Composition 的连接代码，Launch Service 和 Application
无需修改。

## 5. `application_jump_t` 的含义

接口定义在
[`Interfaces/include/firmware/application_jump.h`](../Interfaces/include/firmware/application_jump.h)：

```c
typedef struct
{
    void *context;
    application_jump_execute_fn execute;
} application_jump_t;
```

两个成员分别表示：

- `context`：具体 Provider 的实例指针。当前实现中它指向
  `cortex_m_application_jump_adapter_t`。
- `execute`：真正执行跳转的回调函数。当前实现中它指向 Adapter 内部的 `Execute()`。

调用方只需要这样使用：

```c
application_jump->execute(
    application_jump->context,
    vector_table_address);
```

调用方不需要知道回调属于哪个 Adapter，也不需要知道底层使用了哪些 STM32 寄存器。
这种“接口 + context + 回调”的形式就是本项目中 Adapter 与 Service 之间的主要连接方式。

## 6. Application 最终接收的依赖图

所有 Service 初始化成功后，Composition 构造
`application_dependencies_t`，包括：

| 依赖 | 用途 |
| --- | --- |
| `boot_control` | 读取和提交 EEPROM 中的 Active Record |
| `update` | 准备发布包并安装 APP/GUI Runtime |
| `validation` | 启动前增量校验当前 Runtime |
| `launch` | 配置 XIP 并跳转到 APP |
| `secondary_mcu_update` | Application 驱动 therapy.app.bin 串口升级的外部 MCU Service |
| `package_source` | 挂载、读取和卸载发布卷中的固定文件 |
| `update_request_store` | 加载和清除 Trusted Request |
| `update_request_service` | 解析请求并绑定 Manifest |
| `system_reset` | 安装完成或恢复场景下请求系统复位 |
| `bootloader_version` | 执行最低 Bootloader 版本策略 |

然后调用：

```c
Application_Configure(&application_dependencies);
```

Application 只复制这些指针和值，不复制也不释放指向的对象。因此 Composition 中的
所有对象使用静态生命周期，必须持续有效到整个固件运行结束。

## 7. 主循环中的实际运行

Composition 初始化完成后不会再次参与业务流程。主循环调用
`Application_Process()`，Application 根据状态机逐步驱动 Service：

```text
检查更新介质
    -> 挂载发布卷
    -> 加载并解析 Trusted Request
    -> Prepare 发布包
    -> 执行版本策略
    -> Install selected APP/GUI
    -> Hash-check and program selected therapy.app.bin
    -> 提交 Active Record
    -> 清理 Request 并卸载介质
    -> 校验 Active Runtime
    -> Launch APP
```

每次 `Application_Process()` 只推进一个有界步骤，避免一次调用长时间阻塞主循环。
Composition 不决定这些状态迁移，只提供状态机所需的对象和接口。

Application 只在请求掩码选择 therapy 且 Manifest 提供该组件时打开镜像文件，再把
Manifest 的大小和 SHA-256 与 Composition 的固定目标布局组合成
`secondary_mcu_update_request_t` 传给 Service。therapy 目标地址、容量和擦除页布局
属于 `composition_config.h` 的产品配置，不从 Manifest 或文件读取。

外部 MCU 的预留资源固定通过以下 Package Source 标识选择：

```c
package_source->open(package_source->context, PACKAGE_FILE_THERAPY_APP);
```

该标识对应 `/firmware/therapy.app.bin`。打开成功后，Composition 已注入的
`PackageImageSourceAdapter` 才能读取该文件；文件的关闭和卷的卸载仍由
Application 工作流负责。

## 8. 所有权和生命周期

- Composition 持有 Adapter、Service、Hash Context 和工作缓冲区的实际存储。
- Service 只借用 Composition 注入的接口指针，不负责释放硬件对象。
- `package_source` 与 `update_request_store` 共享同一个 FatFs Volume Context；
  Application 负责挂载和卸载的流程所有权。
- `secondary_mcu_image_source_adapter` 只观察 Package Source 当前已打开的文件，
  不取得 open/close 所有权；文件选择和关闭仍属于 Application 工作流。
- 从 MCU Programmer 与 Secondary MCU Service 由 Composition 持有；Source 原文和
  目标回读使用两块互不重叠、32 字节对齐的 D2 RAM 缓冲区。
- `manifest_hash_interface` 被 Manifest、Request 和 Update Service 分时复用，不能
  并发执行两次摘要计算。
- Active Validation 使用独立的 Hash Context，避免破坏 Manifest/Request 的摘要状态。
- `service_io_buffer` 位于 D2 RAM 并按 32 字节 Cache Line 对齐，供需要 DMA/Cache
  维护的 Service 使用。

## 9. 失败处理原则

Composition 初始化不是事务性的。某一步失败后，前面已经初始化的对象可能仍然存在，
但依赖图不完整，不能继续启动 Application：

```text
任一 Adapter/Service 初始化失败
        |
        v
Composition_Init() 返回错误
        |
        v
composition_initialized 保持为 0
        |
        v
main 调用 Error_Handler()，当前 Boot fail-closed
```

只有全部对象初始化成功，并且 `Application_Configure()` 接受完整依赖图后，才会将
`composition_initialized` 置为非零。

## 10. 修改位置建议

| 需求 | 优先修改位置 |
| --- | --- |
| 修改升级/启动决策 | `Application/src/application.c` |
| 修改安装、校验或启动步骤 | 对应 `Services/src` |
| 更换硬件实现 | 新增或修改 `Adapters` |
| 修改 Adapter 与 Service 的连接 | `Composition/src/composition.c` |
| 修改芯片寄存器或板级设备 | `BSP`、`Platform` 或 CubeMX 生成代码 |
| 增加新的抽象能力 | `Interfaces/include/firmware` |

判断标准很简单：业务规则放上层，硬件细节放下层，连接关系集中放在 Composition。
