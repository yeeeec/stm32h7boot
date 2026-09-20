# STM32H7 Bootloader 升级请求协议

本文档定义 Application 与 Bootloader 之间通过 AT24 Journal 交换升级请求和运行确认的协议。固件包仍由 Application 完成认证并写入 `/UPDATE/firmware`；Bootloader 负责重新校验、安装和状态恢复。

`/CURRENT/firmware` 中保存已确认的 firmware 原版包。本协议不修改该包中的 APP 业务代码、组件内容或 Manifest；升级请求记录也不放入 firmware 包。

## 1. Journal ABI

Journal 使用 AT24 两个 128 字节槽位，记录实际大小为 112 字节。两个槽位均带有 magic、format version、sequence 和 CRC32。读取时选择 CRC 正确且 sequence 更新的记录；写入时写另一槽，完成后回读验证。

共享结构和常量定义在 `Shared/include/firmware/update_journal.h`。

状态只有以下五种：

| 状态 | 含义 |
|---|---|
| `IDLE` | 没有待处理事务，可普通启动 |
| `REQUESTED` | Application 已提交候选包请求 |
| `INSTALLING` | 正在安装 Candidate 或 Rollback |
| `JUMPING` | 安装后的试运行等待 APP 健康确认 |
| `FAILED` | Candidate 和 Rollback 均无法可靠安装或运行，等待受信恢复 |

`source` 为 `CANDIDATE` 时安装源是 `/UPDATE/firmware`；为 `ROLLBACK` 时安装源是 `/CURRENT/firmware`。`JUMPING` 只用于 Candidate 或 Rollback 试运行，普通 `IDLE` 启动不会写入该状态。

## 2. 提交 Candidate

Application 必须按以下顺序执行：

1. 验证外部包的签名、Manifest 和 Manifest 声明的全部组件。
2. 读取 Journal，确认状态为 `IDLE` 且未设置 `CURRENT_COMMIT_PENDING`。
3. 清理旧 `/UPDATE/firmware`，复制完整候选包并对所有文件执行 sync。
4. 从 NVM 重新读取原始 `manifest.json`，计算原始 Manifest SHA-256。
5. 调用 `UpdateService_SubmitCandidate(candidate_version, manifest_sha256)`。
6. 该调用以一次 A/B Journal 写入提交 `REQUESTED`，并回读校验记录。
7. Journal 写入成功后触发一次软件复位。

请求提交前，包可以已经存在于 UPDATE，但不能提前写入 `REQUESTED`。Bootloader 在此期间继续启动当前 APP。

## 3. Bootloader 处理

Bootloader 检测到 `REQUESTED` 后，先校验 UPDATE 的原始 Manifest 摘要、结构、目标、版本和精确文件集合，再校验 CURRENT 是有效的回滚源。通过后持久化 `INSTALLING(CANDIDATE)`，然后按 Manifest 中声明的组件逐个安装。

每次安装前递增并持久化 `install_attempts`，安装从组件起始位置完整重做。成功后清零 `jump_attempts`，持久化 `JUMPING`，并执行 Runtime 向量表检查和 Trial Jump。

Candidate 安装或试运行达到最大次数后，Bootloader 从 CURRENT Manifest 声明的组件集合安装 Rollback。Rollback 安装和试运行均使用独立计数；达到上限后持久化 `FAILED`，不再自动 Jump、擦除或写入 Runtime。

## 4. APP 健康确认

APP 必须在时钟、内存、RTOS、关键任务、配置和必要自检完成后调用：

```c
firmware_status_t UpdateService_ConfirmRunning(
    const uint8_t running_manifest_sha256[32]);
```

接口要求 Journal 当前为 `JUMPING`，`running_manifest_sha256` 必须与 Journal 一致。

- Candidate 确认写入 `IDLE` 并设置 `CURRENT_COMMIT_PENDING`，然后由 APP 触发一次软件复位。
- Rollback 确认写入 `IDLE` 且不设置 Pending，然后由 APP 触发一次软件复位。
- 确认接口不能在只初始化 EEPROM 后立即调用，也不能接受任意版本摘要。

## 5. Commit Pending

Candidate 确认后的下一次启动必须先处理 Pending：

1. 重新验证 UPDATE，并确认其原始 Manifest SHA-256 与 Journal 一致。
2. 删除旧 CURRENT，创建 `/CURRENT/firmware`，从 UPDATE 复制 Manifest 和全部声明组件。
3. 对目标文件执行 sync，并完整验证新的 CURRENT。
4. 只有 CURRENT 验证成功后才删除 UPDATE。
5. 清除 Pending 和候选事务字段，保持 `IDLE`，再普通启动 APP。

任意中断或失败都必须保留 Pending、候选摘要和 UPDATE，以便下次继续。若 UPDATE 已经删除，但 CURRENT 的 Manifest 摘要已与候选摘要一致，则可把该状态视为提交已完成，仅重试 UPDATE 清理和 Pending 清除。不能只因为 CURRENT 自身格式有效就清除 Pending。

## 6. 部分组件包

Manifest 可以声明 APP、GUI、Therapy 中任意非空子集。Bootloader 只安装 Manifest 声明的组件；未声明的目标保持原有 Runtime 内容，不因本次安装或提交而擦除。

CURRENT 保存提交时 UPDATE 中的原始 Manifest 和其声明的组件，不自动拼入旧包中未声明的组件。Candidate 失败后，Rollback 只遍历 CURRENT Manifest 声明的组件；CURRENT 未声明的目标不擦除、不写入、不声称已经恢复。已声明文件缺失、额外文件、大小不符或摘要不符仍会拒绝整个包。

## 7. 接口约束

- `UpdateService_SubmitCandidate()` 是唯一的 Candidate 请求入口。
- `UpdateService_ConfirmRunning()` 是唯一的试运行确认入口。
- `CURRENT_COMMIT_PENDING` 存在时禁止提交新的 Candidate。
- Application 不直接修改 Journal 的状态、计数或 CRC 字段。
- 普通 `IDLE` 启动不挂载文件系统、不写 `JUMPING`，直接验证 Runtime 向量并启动当前 APP。
