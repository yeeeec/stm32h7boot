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
| `manifest_service` | Capability | 已实现 | 严格解析、RFC 8785 受限规范化、SHA-256 和 P-256 签名验证 |
| `relocation_service` | 增量 Capability | 已实现 | APPX Header 和流式重定位 |
| `update_service` | 异步 Use-case | 已实现 | 介质检测、Manifest/源文件校验、APPX 重定位、异步擦写、目标 CRC、Active Record 提交和 Request 清理 |
| `active_validation_service` | 异步 Use-case | 已实现 | 激活槽增量校验 |
| `recovery_service` | 异步 Use-case | 已实现 | 双槽候选校验、可启动槽选择和 Active Record 重建 |
| `launch_service` | 同步、不返回 | 已实现 | XIP、Cache、向量复核和最终跳转 |

Recovery 的候选记录通过 `recovery_candidate_load_fn` 注入。该接口为预留的产品适配点：
当前 EEPROM Active Record 只保存当前激活对，无法在两个副本同时损坏时推导备用槽的完整
元数据；候选加载器必须从产品保留元数据区或其他受信存储返回记录。Recovery 不直接依赖
EEPROM 类型，也不创建动态内存。

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

1. 在 Composition 实例化 `update_service`、`recovery_service` 及其适配器和静态缓冲区；
2. 为 `recovery_candidate_load_fn` 接入产品保留元数据区，并定义候选记录的掉电更新策略；
3. 补齐两个 Use-case 的 Host 掉电注入、完整成功事务、源校验失败和目标写入失败测试；
4. 在真实 W25Q256、AT24C128AN 和 USB MSC 上验证异步擦写、复位重入和 XIP 交接。

Manifest 验签的生产公钥由 Composition 在启动前通过
`Composition_ConfigureManifestVerifier()` 显式注入并复制；未配置公钥时不创建
Manifest verifier，避免测试密钥或占位密钥进入生产固件。公钥不是 Manifest 字段，Key ID
仍由签名对象声明并必须与注入配置完全匹配。
