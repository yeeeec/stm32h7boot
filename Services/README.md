# Services 层设计

## 1. 定位

Services 提供单一、可组合的领域能力。Application 持有业务状态机并编排跨 Service 事务；Composition 创建对象、静态缓冲区和具体 Adapter。

依赖方向固定为：

```text
Application -> Services -> Interfaces
Composition -> Application + Services + Adapters
Adapters -> Interfaces + Platform/BSP/Driver/Middleware
```

Services 不包含 HAL、BSP、Driver、FatFs、USB Host 或 Platform 头文件，不创建 Adapter，不执行动态内存分配，也不通过回调修改 Application 状态。

## 2. 职责边界

Application 负责：

- 介质存在性、挂载和卸载顺序；
- 请求文件探测与提交后的尽力清理；
- 陈旧发布包、最低 Bootloader 版本和版本升级策略；
- Update/Recovery 返回候选后的 Active Record 提交；
- 失败回到旧激活对、恢复、启动和系统复位决策。

Services 负责：

- 严格解析 Manifest 并计算 SHA-256；
- 固定槽布局、边界、版本比较、向量表等可复用规则；
- 镜像源 Hash、裸 APP 外置重定位表、非激活槽擦写和目标 CRC；
- Active Record A/B 的结构校验、选择和原子提交机制；
- 激活镜像和恢复候选的校验；
- XIP 配置、跳转前复核和交接。

Services 不负责：

- Manifest 签名验证、Canonicalization、公钥或 Key ID；
- 请求文件认证、探测、删除或生命周期；
- 升级版本接受/拒绝；
- 决定何时提交 Active Record；
- 介质挂载/卸载或系统复位。

## 3. 模块目录

| 模块 | 形态 | 职责 |
|---|---|---|
| `checked_arithmetic` | 无状态 Capability | 地址、大小和偏移溢出检查 |
| `slot_policy` | 无状态 Capability | 固定分区、配对、非激活槽和范围检查 |
| `version_policy` | 无状态 Capability | 版本排序和严格升级判断，策略调用方是 Application |
| `vector_validation` | 无状态 Capability | MSP、Thumb Reset Handler 和镜像范围检查 |
| `boot_control_service` | 增量 Capability | EEPROM Active Record A/B 读取、选择和原子提交机制 |
| `manifest_service` | Capability | 严格 JSON/Schema 解析和完整 Manifest SHA-256 |
| `relocation_service` | 增量 Capability | Manifest V2 外置表和流式重定位 |
| `update_service` | 异步安装能力 | Prepare Manifest；Install 到非激活槽并返回未提交候选 |
| `active_validation_service` | 异步校验能力 | 指定 Active Record 对应镜像的增量校验 |
| `recovery_service` | 异步恢复能力 | 扫描、验证、选择并返回未提交恢复候选 |
| `launch_service` | 同步、不返回 | XIP、Cache、向量复核和最终跳转 |

Recovery 的候选加载通过 `recovery_candidate_load_fn` 注入。当前 Composition 将它绑定到 Boot Control Service 的保留 Active Record A/B 查询能力；Recovery 本身不知道 EEPROM 地址或 Adapter 类型。

## 4. 生命周期

长流程统一使用：

```text
init -> start -> process -> get_state/get_result -> start
                         \-> cancel（仅明确允许的阶段）
```

`process()` 每次最多推进一个有界步骤。Service 保存内部 stage、offset、请求副本和结果；Application 只读取稳定状态、结果和成功产物。

Update Service 分两段：

- `PrepareStart`：从已经挂载的 Package Source 读取、解析 Manifest；
- `InstallStart`：基于调用方提供的 Active Record 选择非激活槽，完成校验和安装，返回未提交候选。

两段之间的陈旧/版本判断属于 Application。

## 5. 错误与存储规则

`firmware_status_t` 表示 API 或底层操作结果；`boot_error_t` 表示稳定领域失败；`native_error` 仅用于诊断。失败的长流程必须保留终态、稳定错误、stage 和可选 native error。

外部 Flash 通过 `async_block_device_t` 增量执行。一次只启动一个物理 program page 或 erase unit，后续主循环轮询完成；Service 不阻塞等待，也不负责喂看门狗。

EEPROM 只包含两个 256-byte Active Record。Boot Control Service 实现结构/CRC/提交标记校验、RFC 1982 序列选择以及掉电安全交替提交；提交时机由 Application 决定。
