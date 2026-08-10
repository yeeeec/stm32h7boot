# Services

`Services` 是 Application 编排层使用的固定 Runtime 启动与升级能力集合。它只
依赖稳定的 Interfaces 和领域类型，不应包含 HAL、FatFs、CubeMX Handle、BSP 或
具体器件类型。

量产依赖方向：

```text
Application -> Services -> Interfaces
Composition -> Application + Services + Adapters
```

量产服务集合：

- `boot_control_service`：EEPROM A/B Active Record V2 的读取和原子提交。
- `manifest_service`：冻结 schema 的 Manifest 严格解析、字段校验和摘要生成。
- `update_request_service`：受信升级请求的严格解析及其与 Manifest 的绑定。
- `update_service`：源文件校验、固定 APP/GUI Runtime 安装及候选 Active Record
  生成。
- `secondary_mcu_update_service`：通过注入的镜像 Source 和 MCU Programmer，
  按页擦除、分块写入并逐块回读校验外部 MCU 固件；它不绑定具体 ROM 协议。
- `active_validation_service`：已安装 APP/GUI 的 SHA-256 与 APP 向量表校验。
- `launch_service`：固定 Runtime 的 XIP 建立、Cache 失效和 Application 交接。
- `runtime_layout`、`vector_validation`、`version_policy` 和 checked arithmetic：
  可复用、无上层业务决策的领域能力。

Application 拥有请求生命周期、版本策略、提交时机、介质清理、复位以及在没有
授权发布包时的 fail-closed 决策。Update Service 拥有包读取、源/目标哈希、Flash
写入和固定 Runtime 边界校验。Service 不得反向回调或直接改变 Application 顶层
状态；所有对象由 Composition 静态创建并注入。

更新安装开始和首次 Runtime 擦除前，`update_service` 都会通过
`xip_controller_t` 查询 QSPI 状态；若仍处于 memory-mapped 模式，则退出并再次
确认已回到 indirect 模式，才允许任何 Runtime 擦写。源文件的 open 状态只会在
底层 close 成功后释放；失败时服务有限重试关闭并把真实状态保留给上层清理。
Application 同样只在 Package Source
确认 unmount 成功后清除 mounted 状态，重试耗尽后保持 fail-closed。若安装已
修改 Runtime 或 XIP 退出无法恢复，清理成功后会保留 trusted request 并复位到
下一轮恢复安装，而不是尝试启动可能不完整的镜像。
