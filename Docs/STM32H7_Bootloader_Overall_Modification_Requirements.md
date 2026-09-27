# STM32H7 Bootloader 总体修改要求

## 1. 基线

当前代码基线：

```text
Repository: yeeeec/stm32h7boot
Branch: main
Baseline Commit: cbec0bd1c6aea29323921bd47a29916c6febba02
```

本轮不是继续增加恢复状态、策略层或通用框架，而是在现有 Bootloader 上做一次最终收敛：

1. 重新定义 `BOOTLOADER_UPDATE_DEBUG_MODE`。
2. 简化 Production Journal 行为。
3. 增加 Journal 损坏时基于 CURRENT 的确定性恢复。
4. 删除内部重复错误体系。
5. 固定 `PackageReader_Validate()` 为本项目唯一的完整 Package 校验接口。
6. 删除 `update_internal.h` 的“大总管”模式。
7. 删除无独立价值的薄封装和通用化接口。
8. 保留真正有明确职责边界的模块，不新增 Manager / Strategy / Repository 等抽象层。

最终目标：

> Bootloader 只保留本产品真正需要的升级、回滚、快照和恢复逻辑；代码结构清楚，但不做通用升级框架。

---

# 2. 总体设计原则

本项目为固定硬件、固定组件、固定升级流程，不追求通用性和可移植框架。

必须遵守：

```text
一个产品
一套 Package 格式
一套组件集合
一套升级流程
一套恢复规则
```

禁止为了未来可能需求增加：

```text
Manager
Strategy
Repository
Provider
Policy interface
多套 Package 校验模式
多套 Installer
兼容旧流程的 wrapper
未使用的可选参数
```

模块拆分只服务于真实职责，不服务于“架构形式”。

---

# 3. 最终总体架构

```text
+---------------------------------------------------+
|                  BootFlow_Run()                   |
|                                                   |
|       UpdateService_Process()                     |
|                  |                                |
|                  v                                |
|       RuntimeImage_Prepare()                      |
|                  |                                |
|                  v                                |
|                Jump APP                           |
+---------------------------------------------------+
                   |
                   v
+---------------------------------------------------+
|                Update Service                     |
|                                                   |
|  Debug Mode              Production Mode          |
|  ----------              ---------------          |
|  Detect UPDATE           Read Journal             |
|  Validate Package        State Dispatch           |
|  Install                 Update/Rollback          |
|  Rebuild CURRENT         CURRENT Recovery         |
|                                                   |
+---------------------------------------------------+
        |             |             |             |
        v             v             v             v
 PackageReader   ImageInstaller  CurrentStore  UpdateJournal
        |                                      
        v                                      
 ComponentRegistry / Manifest                  
        |
        v
 Platform Storage / Flash / CPU / Hash
```

`BootFlow` 不理解：

```text
Journal 状态
CURRENT/LAST
Debug 自动升级
版本策略
Package 结构
恢复策略
```

这些全部留在 `Services/Update`。

---

# 4. BOOTLOADER_UPDATE_DEBUG_MODE 最终定义

## 4.1 Debug 模式不使用 Journal

当：

```c
BOOTLOADER_UPDATE_DEBUG_MODE == 1U
```

Debug 模式完全不使用 Journal 作为事务机制。

不得：

```text
读取 Journal 决定是否升级
根据 PENDING/WRITING/JUMPING 恢复
初始化 Journal
修复 Journal
升级完成后复位 Journal
为了 Debug 写 IDLE/PENDING/WRITING/JUMPING
```

Debug 模式的目的只有一个：

> 开发阶段启动时直接检测 UPDATE Package，有合法升级包就安装，没有合法升级包就启动当前程序。

---

# 5. Debug 模式完整流程

```text
Boot
 |
 v
Mount SD
 |
 v
检查 /UPDATE/firmware/manifest.json
 |
 +-----------------------------+
 |                             |
不存在 / Package 不符合         Package 完整合法
 |                             |
 v                             v
Unmount                    执行 Upgrade
 |                             |
 v                             v
RuntimeImage_Prepare       Install Components
 |                             |
 v                             v
Jump APP                   Runtime Verify
                               |
                               v
                         Rebuild CURRENT
                               |
                               v
                         Verify CURRENT
                               |
                               v
                         Cleanup UPDATE
                               |
                               v
                         Unmount
                               |
                               v
                         RuntimeImage_Prepare
                               |
                               v
                            Jump APP
```

Debug 模式：

- UPDATE 不存在：正常跳转。
- `manifest.json` 不存在：正常跳转。
- Package 不符合完整升级要求：不执行升级，正常跳转。
- Package 合法：执行完整升级。
- 不进行 Journal 风险恢复。
- 不考虑升级过程中掉电后的恢复。

但当前一次升级过程中发生 Flash/Storage 等实际执行错误时，不允许把失败伪装成升级成功；记录日志后返回失败或停止当前升级路径。

---

# 6. Debug 模式版本规则

Debug 模式主要用于研发升级验证，不需要 Journal 事务语义。

Package 自身仍必须满足：

```text
Manifest schema
product
hardware
minimum_bootloader_version
完整组件集合
文件集合
size
SHA256
```

是否保留 `UPDATE >= CURRENT` 的版本限制，以现有产品需求为准；若研发阶段需要反复安装旧版本，Debug 可直接允许合法 Package 安装，不为此增加额外 Policy 模块。

不得增加 Debug 专用状态机。

---

# 7. Production 模式总原则

当：

```c
BOOTLOADER_UPDATE_DEBUG_MODE == 0U
```

Production 使用 Journal。

流程入口：

```text
Boot
 |
 v
Read Journal
 |
 +-------------------------------+
 |               |               |
合法状态       Journal 非法       Journal 不可恢复读取
 |               |               |
 v               v               v
状态机处理    CURRENT Recovery   CURRENT Recovery
```

Production 不通过 UPDATE 文件存在与否自动发起升级。

升级/回滚请求由 Journal 决定。

---

# 8. Production Journal 状态

继续使用当前 ABI：

```text
IDLE
PENDING
WRITING
JUMPING
```

Target：

```text
NONE
UPDATE
ROLLBACK
```

不要增加：

```text
FAILED
RETRY
RECOVERY
COMMIT_PENDING
attempt count
error count
```

---

# 9. IDLE

```text
IDLE/NONE
   |
   v
不访问 UPDATE/CURRENT/LAST
   |
   v
RuntimeImage_Prepare
   |
   v
Jump APP
```

正常启动路径必须保持短路径。

---

# 10. Upgrade 请求

收到：

```text
PENDING/UPDATE
```

执行：

```text
Validate UPDATE
 |
 v
读取 CURRENT
 |
 +-----------------------------+
 |                             |
CURRENT 不存在                 CURRENT 有效
 |                             |
 v                             v
First Install              Version Check
不创建 LAST                  |
 |                           v
 |                     CURRENT -> LAST
 |                           |
 +-------------+-------------+
               |
               v
         WRITING/UPDATE
               |
               v
       完整安装 UPDATE
               |
               v
        Runtime Verify
               |
               v
       Rebuild CURRENT
               |
               v
        Verify CURRENT
               |
               v
       JUMPING/UPDATE
               |
               v
           Jump APP
```

升级过程中 Reset/掉电后，如果 Journal 仍为：

```text
PENDING/UPDATE
WRITING/UPDATE
```

重新执行完整升级流程，不实现组件级续传和局部恢复。

---

# 11. Upgrade 版本策略

Production Upgrade：

```text
UPDATE > CURRENT  -> allow
UPDATE = CURRENT  -> allow
UPDATE < CURRENT  -> reject
```

First Install：

```text
CURRENT 不存在 -> allow
```

CURRENT 目录存在但 Package 损坏，不属于 First Install。

---

# 12. Rollback 请求

收到：

```text
PENDING/ROLLBACK
```

执行：

```text
Validate LAST
 |
 v
WRITING/ROLLBACK
 |
 v
完整安装 LAST
 |
 v
Runtime Verify
 |
 v
Rebuild CURRENT from LAST
 |
 v
Verify CURRENT
 |
 v
IDLE/NONE
 |
 v
Jump APP
```

为保持设计简单，本方案规定：

> `JUMPING` 只用于 Upgrade 成功后的 APP 启动确认窗口。

Rollback 是显式恢复操作，成功完成后直接恢复：

```text
IDLE/NONE
```

不再给 Rollback 增加第二个“启动确认/失败再回退”层。

这样可以避免 `JUMPING/ROLLBACK` 的语义歧义。

---

# 13. JUMPING/UPDATE

`JUMPING/UPDATE` 表示：

```text
UPDATE 已完整安装
CURRENT 已更新为新版本
LAST 保存升级前版本
Bootloader 已经尝试启动新 APP
但 APP 尚未确认 IDLE
```

如果下一次 Boot 仍读取到：

```text
JUMPING/UPDATE
```

认为新版本未成功完成 APP 确认。

处理：

```text
Validate LAST
 |
 v
完整安装 LAST
 |
 v
Runtime Verify
 |
 v
Rebuild CURRENT from LAST
 |
 v
Verify CURRENT
 |
 v
IDLE/NONE
 |
 v
Jump APP
```

即：

> 新版本启动确认失败时，自动恢复升级前 CURRENT 版本。

不要重新安装 UPDATE。

---

# 14. APP 确认

正常 Upgrade 成功后：

```text
Bootloader -> JUMPING/UPDATE -> Jump APP
```

APP 完成规定的初始化确认后，将 Journal 写为：

```text
IDLE/NONE
```

Bootloader 不负责 Production Upgrade 的成功确认。

---

# 15. Journal 非法时的恢复原则

Production 读取 Journal 时，如果出现：

```text
magic invalid
version invalid
record_size invalid
CRC invalid
state/target invalid
A/B sequence conflict
两槽均无合法 Record
```

不直接假定 IDLE，也不根据 UPDATE 自动升级。

统一进入：

```text
CURRENT Recovery
```

---

# 16. CURRENT Recovery

先完整验证：

```text
/CURRENT/firmware
```

必须确认 CURRENT Package 本身合法。

如果 CURRENT 不存在或损坏：

```text
Recovery Failed
```

不得猜测 UPDATE 或 LAST 哪个才是正确版本。

不得自动切换成 Rollback。

---

# 17. 当前运行内容与 CURRENT 对比

CURRENT 合法后，读取 CURRENT Manifest 中所有组件：

```text
app
gui
therapy
voice
config
```

根据每个组件的目标地址和 `size`：

```text
读取实际已安装区域
 -> 计算 SHA256
 -> 与 CURRENT manifest.sha256 比较
```

必须比较完整组件集合，不能只验证 APP vector。

---

# 18. Journal 非法但 Runtime 与 CURRENT 一致

如果：

```text
所有实际安装组件 SHA256
==
CURRENT Package Manifest
```

说明当前运行内容是完整 CURRENT，只是 Journal 损坏。

处理：

```text
Reset Journal -> IDLE/NONE
 |
 v
RuntimeImage_Prepare
 |
 v
Jump APP
```

不重复写固件。

---

# 19. Journal 非法且 Runtime 与 CURRENT 不一致

任意组件 mismatch：

```text
不要只修 mismatch 组件
```

直接：

```text
使用 CURRENT 完整重装所有组件
 |
 v
Runtime Verify
 |
 v
重新对全部组件计算 SHA256
 |
 v
确认全部匹配 CURRENT
 |
 v
Reset Journal -> IDLE/NONE
 |
 v
Jump APP
```

这里的 CURRENT 是唯一恢复基准。

---

# 20. Journal Reset

Journal 需要一个明确的“强制重新建立合法记录”的内部能力。

例如：

```c
firmware_status_t UpdateJournal_ResetIdle(void);
```

语义：

```text
不依赖现有 A/B Record 可读
Erase Journal slots
建立新的 IDLE/NONE Record
写入
回读校验
```

仅用于：

```text
Production Journal 非法且 CURRENT Recovery 成功
```

Debug 模式不使用该接口。

不要把“强制重建 Journal”混入正常 `UpdateJournal_WriteState()`。

---

# 21. PackageReader_Validate() 去通用化

当前形式：

```c
PackageReader_Validate(
    root,
    expected_manifest_digest,
    verify_payload_hashes,
    package);
```

本项目不需要多种校验模式。

修改为唯一接口：

```c
firmware_status_t PackageReader_Validate(
    const char *root,
    update_package_t *package);
```

固定执行全部检查：

```text
Manifest read
Manifest parse
Manifest schema
product/hardware
minimum bootloader version
完整 component set
exact file set
size
SHA256
组件地址范围
```

删除：

```text
expected_manifest_digest
verify_payload_hashes
ValidateFast
ValidateManifestOnly
ValidateWithoutHash
其它同义 wrapper
```

UPDATE、CURRENT、LAST 全部走同一个完整校验函数。

---

# 22. 内部错误体系简化

当前存在：

```text
firmware_status_t
update_failure_t
update_operation_result_t
```

内部模块不再同时携带两套错误。

删除：

```c
update_operation_result_t
```

内部函数统一返回：

```c
firmware_status_t
```

例如：

```c
firmware_status_t PackageReader_Validate(...);
firmware_status_t ImageInstaller_Install(...);
firmware_status_t CurrentStore_Verify(...);
firmware_status_t RuntimeVerifier_Validate(...);
```

详细错误直接在发生错误的模块记录日志。

不要为了把诊断信息逐层上传而制造复合返回值。

---

# 23. update_result_t 收敛

`UpdateService_Process()` 是 BootFlow 与 Update 模块之间的业务边界。

建议最终只保留：

```c
typedef struct
{
    update_outcome_t outcome;
    firmware_status_t status;
} update_result_t;
```

如果确认没有外部代码依赖 `update_failure_t`，删除：

```text
update_failure_t
update_result_t.failure
```

BootFlow 记录：

```text
outcome
status
```

详细原因查看 Update 模块日志。

不要保留一套仅用于重复描述日志的 Failure 枚举。

---

# 24. update_internal.h 删除

当前 `update_internal.h` 同时暴露：

```text
所有共享类型
Manifest API
PackageReader API
ComponentRegistry API
ImageInstaller API
CurrentStore API
RuntimeVerifier API
Journal API
VersionPolicy API
Hex helper
```

这使所有 Update `.c` 文件都可以看到整个子系统，模块边界失去意义。

本轮删除：

```text
Services/Update/src/update_internal.h
```

---

# 25. 内部头文件原则

不要为了拆 `update_internal.h` 又创建大量抽象头。

只为真实跨 `.c` 依赖提供最小内部头文件。

建议：

```text
update_model.h
    只放共享数据结构和固定常量

package_reader.h
    PackageReader_Validate

current_store.h
    CURRENT/LAST 接口

image_installer.h
    ImageInstaller_Install

runtime_verifier.h
    Runtime 校验/对比接口

update_journal_internal.h
    Journal 内部接口
```

每个头文件只暴露真实需要跨模块调用的函数。

不要在这些头里重新建立“统一总入口”。

---

# 26. Manifest 内部接口

`update_manifest.c` 保留。

Manifest parser/canonical/digest 等只被 PackageReader 使用的接口，应尽量保持 PackageReader 内部依赖。

如果只有 `package_reader.c` 调用某些 Manifest helper：

- 不放入公共 include。
- 不放进新的总头文件。
- 可以放最小 `update_manifest_internal.h`，或在不破坏清晰度的前提下进一步合并。

不要把 Manifest 做成独立通用 SDK。

---

# 27. VersionPolicy 简化

当前 `version_policy.c` 很薄。

本轮优先删除该模块。

规则直接放在真正使用它的位置：

```text
minimum_bootloader_version
    -> Package/Manifest 校验流程

UPDATE >= CURRENT
    -> update_service.c 的 PENDING/UPDATE 准备阶段
```

保留一个简单的：

```c
UpdateVersion_Compare()
```

即可。

不要为两个固定 `if` 条件保留 Policy 模块。

---

# 28. ComponentRegistry 保留

`component_registry.c` 保留。

它承载的是真实产品配置：

```text
组件名称
目标地址
最大尺寸
安装顺序
mask
format
```

这是必要的数据集中点，不属于过度封装。

不要继续抽象成动态注册、插件或 provider。

---

# 29. CurrentStore 保留

`current_store.c` 继续只负责：

```text
Verify CURRENT
Read CURRENT
Save CURRENT -> LAST
Rebuild CURRENT from source
Verify LAST
Cleanup UPDATE（如仍实际需要）
```

不要让 CurrentStore：

```text
判断 Journal
决定 UPDATE/ROLLBACK
决定版本策略
决定 Runtime 是否启动
```

---

# 30. RuntimeVerifier 扩展但不分层

当前 `RuntimeVerifier_Validate()` 保留，用于基本 Runtime vector 检查。

为 Journal invalid 的 CURRENT Recovery 增加一个直接接口即可，例如：

```c
firmware_status_t RuntimeVerifier_ComparePackage(
    const update_package_t *package,
    uint32_t *mismatch_mask);
```

职责只有：

```text
按 Package Manifest
读取实际安装区域
计算 SHA256
返回是否与 Package 一致
```

不要再增加：

```text
RuntimeRecoveryManager
RuntimeIntegrityService
ComponentHealthPolicy
```

---

# 31. ImageInstaller 保持单一

Upgrade、Rollback、CURRENT Recovery 都必须复用同一套 Installer。

不要出现：

```text
InstallUpdate()
InstallRollback()
InstallRecovery()
```

统一按：

```text
source Package + component descriptor
```

完成安装。

差异由 UpdateService 决定 Source：

```text
Upgrade  -> UPDATE
Rollback -> LAST
Recovery -> CURRENT
```

---

# 32. UpdateService 最终职责

`update_service.c` 是唯一业务编排者。

负责：

```text
Debug / Production 入口选择
Production Journal state dispatch
Upgrade/rollback source 选择
First Install 判断
版本比较
JUMPING/UPDATE 回滚
Journal invalid CURRENT Recovery
最终 Journal 状态切换
```

不负责：

```text
SD/FATFS 底层操作
Flash 擦写实现
SHA256 算法
Manifest JSON 解析细节
Journal Sector 写入细节
```

这里允许有明确的流程代码。

不要为了让 `update_service.c` 变短，再把每一段流程包装成 Manager 对象或 Strategy。

---

# 33. 最终 Production 状态图

```text
                           +----------------+
                           |   IDLE/NONE    |
                           +-------+--------+
                                   |
                                   | APP request UPDATE
                                   v
                           +----------------+
                           | PENDING/UPDATE |
                           +-------+--------+
                                   |
                                   v
                           +----------------+
                           | WRITING/UPDATE |
                           +-------+--------+
                                   |
                                   | install success
                                   v
                           +----------------+
                           | JUMPING/UPDATE |
                           +---+--------+---+
                               |        |
                    APP confirm|        | reset before confirm
                               |        |
                               v        v
                         IDLE/NONE   Install LAST
                                      |
                                      v
                                   IDLE/NONE


APP request ROLLBACK
        |
        v
+------------------+
| PENDING/ROLLBACK |
+--------+---------+
         |
         v
+------------------+
| WRITING/ROLLBACK |
+--------+---------+
         |
         | install LAST success
         v
     IDLE/NONE
         |
         v
      Jump APP
```

---

# 34. Journal invalid 恢复图

```text
Read Journal
    |
    v
Journal invalid
    |
    v
Validate CURRENT
    |
    +------------------------+
    |                        |
 invalid/missing            valid
    |                        |
    v                        v
 Recovery Failed      Compare Runtime Hash
                              |
                 +------------+------------+
                 |                         |
              all match                mismatch
                 |                         |
                 v                         v
          Reset Journal IDLE      Install CURRENT All
                 |                         |
                 |                  Runtime Verify
                 |                         |
                 |                  Compare Again
                 |                         |
                 +------------+------------+
                              |
                              v
                       Reset Journal IDLE
                              |
                              v
                           Jump APP
```

---

# 35. Debug / Production 边界

必须做到：

```text
DEBUG MODE
    不依赖 Journal
    不进行 Journal recovery
    UPDATE 合法就安装
    UPDATE 不合法/不存在就跳转

PRODUCTION MODE
    不自动扫描 UPDATE 发起事务
    Journal 正常 -> 状态机
    Journal 异常 -> CURRENT Recovery
```

不要让两个模式互相借用特殊逻辑。

---

# 36. 删除项

本轮检查并删除：

```text
update_operation_result_t
expected_manifest_digest 参数
verify_payload_hashes 参数
无调用者 Package validate wrapper
Debug Journal 初始化逻辑
Debug PENDING/WRITING/JUMPING 恢复逻辑
Debug Journal confirm/reset 逻辑
无实际消费者的 update_failure_t（确认后删除）
update_internal.h
version_policy.c（规则并入实际调用点后）
无调用者内部 helper/API
旧 Debug JUMPING 特殊逻辑
```

不要保留兼容层。

---

# 37. 保留模块

最终 Update 子系统应大致保留：

```text
update_service.c
update_journal.c
package_reader.c
update_manifest.c
component_registry.c
image_installer.c
current_store.c
runtime_verifier.c
```

是否保留独立小文件，以真实代码量和依赖为准。

原则：

> 有明确独立职责才拆文件；只有一两个简单条件且只有一个调用点，不单独造模块。

---

# 38. 测试要求

## Debug

验证：

```text
UPDATE 不存在 -> Jump
manifest 不存在 -> Jump
Package 非法 -> Jump
Package 合法 -> Install -> CURRENT -> Jump
Debug 不读取 Journal 决定事务
Debug 不写 Journal
```

## Production

验证：

```text
IDLE -> Jump
PENDING/UPDATE -> Upgrade
WRITING/UPDATE reset -> 完整重做 Upgrade
Upgrade success -> JUMPING/UPDATE
APP confirm -> IDLE
JUMPING/UPDATE reset -> Install LAST -> IDLE
PENDING/ROLLBACK -> Install LAST -> IDLE
WRITING/ROLLBACK reset -> 完整重做 Rollback
```

## Journal invalid

验证：

```text
Journal invalid + CURRENT valid + runtime match
    -> Journal IDLE -> Jump

Journal invalid + CURRENT valid + runtime mismatch
    -> Full install CURRENT -> verify -> IDLE -> Jump

Journal invalid + CURRENT invalid/missing
    -> Recovery Failed
```

## PackageReader

验证唯一接口固定执行：

```text
Manifest
Target
Version requirement
Complete components
Exact files
Size
SHA256
```

---

# 39. 构建要求

完成后必须：

```text
Debug clean build PASS
Release clean build PASS
Host tests PASS
No new warning
```

如果当前没有 STM32 实机：

```text
A/B internal flash
cache readback
真实掉电
```

继续作为后续硬件验证项，不因为没有实机重新增加软件抽象。

---

# 40. 最终验收标准

满足以下条件后，本轮修改完成：

1. Debug 模式完全脱离 Journal 事务机制。
2. Debug 仅负责检测合法 UPDATE 并安装。
3. Production 只由 Journal 驱动 Upgrade/Rollback。
4. Upgrade 成功进入 `JUMPING/UPDATE`。
5. `JUMPING/UPDATE` 未确认再次启动时恢复 LAST。
6. Rollback 成功直接进入 `IDLE/NONE`。
7. Journal invalid 时以 CURRENT 为唯一恢复基准。
8. Runtime 与 CURRENT 不匹配时完整重装 CURRENT。
9. CURRENT 自身无效时不猜测恢复源。
10. 不增加新的 Journal 状态。
11. `PackageReader_Validate()` 只有一种完整校验模式。
12. 删除 `update_operation_result_t`。
13. 内部函数统一使用 `firmware_status_t`。
14. `update_failure_t` 若无真实外部用途则删除。
15. 删除 `update_internal.h` 大总管头文件。
16. 删除 `version_policy.c` 薄封装。
17. 不新增 Manager/Strategy/Repository 等框架层。
18. BootFlow 继续保持薄。
19. UpdateService 是唯一业务编排者。
20. Debug/Release clean build 和 Host tests 全部通过。

---

# 41. Codex 完成后输出

```text
Commit SHA

修改文件
新增文件
删除文件

Debug 最终流程
Production 最终流程
Journal invalid CURRENT Recovery 流程
JUMPING/UPDATE 回滚流程
Rollback 最终流程

删除的过度封装项
PackageReader 最终接口
内部错误体系最终结构
内部头文件最终结构

Debug clean build
Release clean build
Host tests
Warnings

未完成的实机验证项
```

