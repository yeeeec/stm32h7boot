# Services 层设计

## 1. 当前定位

Services 是当前 Bootloader 实现的主体。Application 暂时只保留生命周期入口，不保存
升级阶段，也不访问 Interface。具体 Adapter 和长期 Buffer 仍由 Composition 创建和持有。

依赖方向固定为：

```text
Application -> Use-case Service -> Capability -> Interfaces
Composition -> Services + Adapters
Adapters -> Interfaces + Platform/BSP/Driver/Middleware
```

Services 不获得 HAL、BSP、Driver、FatFs 或 USB Host include path，不创建 Adapter，不使用
动态内存，也不通过回调修改 Application 状态。

## 2. 目录和 API

```text
Services/
  include/services/common/       稳定领域类型、生命周期和错误
  include/services/capability/   可复用规则和同步能力
  include/services/use_case/     完整长流程的公共 API
  internal/services/             Composition 可见的构造 API 和对象布局
  src/capability/                 Capability 实现
  src/use_case/                   Use-case 状态机实现
```

只有存在实现的模块才创建目录或文件。无状态规则不使用机械化的
`Init + Process + GetState` 接口。

## 3. 服务目录

| 模块 | 形态 | 状态 | 职责 |
|---|---|---|---|
| `checked_arithmetic` | 无状态 | 已实现 | 地址、大小和偏移溢出检查 |
| `slot_policy` | 无状态 | 已实现 | 固定分区、配对、非激活槽和范围检查 |
| `version_policy` | 无状态 | 已实现 | `major.minor.patch` 排序和严格升级判断 |
| `vector_validation` | 无状态 | 已实现 | MSP、Thumb Reset Handler 和镜像范围检查 |
| `boot_control_service` | 增量 Capability | 已实现 | EEPROM A/B 记录选择、校验和原子提交 |
| `manifest_service` | Capability | 待实现 | 严格解析、兼容性、Hash 和签名验证 |
| `relocation_service` | 增量 Capability | 已实现 | APPX Header 和流式重定位 |
| `update_service` | 异步 Use-case | 待实现 | APP/GUI 原子升级事务 |
| `active_validation_service` | 异步 Use-case | 已实现 | 激活槽增量校验 |
| `recovery_service` | 异步 Use-case | 待实现 | 备用槽恢复和记录重建 |
| `launch_service` | 同步、不返回 | 已实现 | XIP、Cache、向量复核和最终跳转 |

“待实现”模块不提供返回成功的占位代码。只有依赖契约、错误映射和测试向量确定后才加入
构建，避免上层误认为安全能力已经存在。

## 4. Use-case 生命周期

长流程统一使用：

```text
init -> start -> process -> get_state/get_result -> start
                         \-> cancel（仅在服务明确允许的阶段）
```

`process()` 每次最多执行一个状态转换、一次短读写、启动一个擦除单元或轮询一次异步
操作。Service 保存内部 stage、offset、请求副本和结果；Application 只能读取外部状态及稳定
错误。

## 5. 错误规则

`firmware_status_t` 表示 API 调用是否有效或底层操作是否成功；`boot_error_t` 表示
Application 可用于决策的稳定失败类别；`native_error` 只用于日志和诊断。

Use-case 失败后必须保存：

- 外部状态 `SERVICE_RUN_STATE_FAILED`；
- 稳定 `boot_error_t`；
- 失败 stage；
- 可选 native error。

## 6. 存储执行规则

Services 使用 `async_block_device_t`。编程和擦除分别通过 `program_start()`、
`erase_start()` 启动，并由后续主循环调用 `poll()`；一次启动只允许一个物理 page 或擦除
单元。`cancel == NULL` 表示器件不能中止已经发出的命令，Use-case 仍需等待操作进入终态后
才能复用设备。

看门狗由 `Platform_Process()` 统一刷新。Service 不主动刷新看门狗；因此任何无界
`process()`、Driver 等待或 Middleware 调用都会暴露为系统健康故障。

## 7. 下一实施批次

1. 冻结 CRC 参数和 AT24C128AN 板级参数，实现 checksum 与 boot-control store Interface；
2. 实现 `boot_control_service` 及逐页掉电注入测试；
3. 冻结密码学库、公钥和 RFC 8785 测试向量，实现 Manifest Capability；
4. 实现 APPX Header 与 relocation Capability；
5. 实现 `update_service` 状态机，再实现 Validation、Recovery 和 Launch。
