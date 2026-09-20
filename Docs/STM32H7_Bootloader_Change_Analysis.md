# STM32H7 Bootloader 需求基线变更分析

分析日期：2026-09-20。依据：[STM32H7_Bootloader_Requirements_Baseline.md](STM32H7_Bootloader_Requirements_Baseline.md)。

范围：当前工作区的 Bootloader、Services、Platform、设备适配及构建配置。结论来自静态代码对照，未执行编译、板级试验或掉电测试。CURRENT 保存 firmware 原版内容，不因备份、提交或回滚而修改其中的 APP、组件文件或原始 Manifest。

本次澄清后的分析口径：

- “唯独没有升级请求文档”按用户说明记录为本次文档补充项；该文档是协议/设计交付物，不改写 CURRENT 中的原版 firmware，也不应被混入 firmware 包。请求记录仍沿用基线的 AT24 Journal。
- 允许部分组件包。“对应内容不做回滚”暂按 CURRENT 包未声明的组件不回滚理解：回滚遍历 CURRENT Manifest 已声明的组件，未声明的目标不擦除、不写入。例如 CURRENT 仅有 APP、GUI，则回滚不操作 Therapy。
- 部分包回滚范围按“CURRENT Manifest 未声明的组件不处理”记录；若产品实际指的是“已更新组件也不回滚”，只需调整该条状态机规则，不能沿用整包恢复实现。
- 撤回“必须改造 CURRENT 中旧 APP”和“部分包必须合并成三组件完整 CURRENT”的结论。保存原版固件包与运行时健康确认能力是不同事项，不能由前者直接推导出需要修改固件。

**1. 总体结论**

现有底层安装、校验和分层结构具备复用价值；主要工作集中在升级事务协议、持久状态机和 CURRENT 提交时机，属于启动升级流程的实质调整。

当前流程：

```text
读取旧 Journal → 有未完成事务就恢复 CURRENT
→ 取出并清除 RAM mailbox 请求
→ 验证 UPDATE → 写 INSTALLING → 安装
→ 写 COMMITTING → CURRENT_NEW / CURRENT.previous 目录切换
→ 清 Journal → 复位 → 启动 APP
```

目标流程：

```text
读取 AT24 A/B Journal
→ REQUESTED 预检 → INSTALLING(CANDIDATE)
→ JUMPING(CANDIDATE) → APP 健康确认
→ IDLE + CURRENT_COMMIT_PENDING → 复位
→ UPDATE 复制并验证为 CURRENT → 清理 UPDATE / Pending → 普通启动

Candidate 安装或试运行达到上限
→ INSTALLING(ROLLBACK) → JUMPING(ROLLBACK)
→ APP 确认 → IDLE，保留原 CURRENT
Rollback 安装或试运行达到上限 → 持久 FAILED
```

当前最关键的行为冲突是：**新 APP 尚未证明能够正常运行，旧 CURRENT 就已经被替换并删除了。** 因此，现有“安装成功”不能继续作为“升级事务完成”的依据。

**2. 变更优先级与代码定位**

P0 表示基线主流程成立所必需；P1 表示可靠性、边界和验收必须补齐的内容。P1 也需要在交付前完成。

| 优先级 | 项目 / 基线章节 | 当前实现与差异 | 需要的变更 |
|---|---|---|---|
| P0 | 请求通道，§3、5、8 | `boot_mailbox.c` 的 `UpdateBootMailbox_Take()` 只接受软件复位，读取后立即清除 retained RAM 请求 | 用 AT24 Journal 的 REQUESTED 作为唯一升级请求；包写入、sync、回读摘要全部成功后最后提交请求 |
| P0 | Journal 模型，§6 | `update_internal.h` 仅定义 NONE / INSTALLING / COMMITTING；52 字节记录仅有 phase 和一个摘要 | 增加五状态、source、flags、三类计数、last_error、candidate_version、candidate/running 摘要；提升格式版本并共享 ABI |
| P0 | 启动状态机，§9—13 | `update_service.c` 的 `UpdateService_RecoverInterrupted()` 对所有未完成事务统一恢复 CURRENT | 按状态与 source 分流；安装中断从所选包起始位置重装；试运行失败只重试 Jump，达到上限才切换 Rollback |
| P0 | APP 确认，§14 | 本仓库尚无 JUMPING 流程和健康确认接口；未检查业务 APP 工程 | 补充 Bootloader/共享服务接口并核对已有 APP 协议；Candidate 确认置 Pending，Rollback 确认不置 Pending。CURRENT 中的固件保持原样，不把修改旧 APP 列为已确定工作 |
| P0 | CURRENT 提交，§5、10、15 | `CurrentStore_Commit()` 在安装后立即执行，依赖 CURRENT_NEW 和 CURRENT.previous | 改为确认后 UPDATE → CURRENT；Pending 下支持复制、验证、删除 UPDATE 各阶段掉电恢复 |
| P0 | 信任边界，§2、3、7 | `PackageReader_Validate()` 计算 canonical 摘要并调用 `ManifestVerify_Verify()`，执行 ECDSA | Bootloader 删除签名验证路径及信任公钥配置，保留原始 Manifest SHA-256、严格解析、组件 SHA-256 和 readback |
| P0 | 安装前恢复保障，§11 | CURRENT 不存在时允许继续，由 `VersionPolicy_Check()` 放行首次安装 | 擦除前完整验证 CURRENT 所声明的文件及摘要；包缺失或声明文件无效则拒绝。合法部分包未声明某组件不等于包损坏 |
| P0 | FAILED，§17 | `BOOT_FLOW_FATAL` 最终进入 `Error_Handler()`，无持久失败状态和救援处理 | 新增稳定安全循环；保留错误、source、计数，不自动 Jump/擦写；按产品策略喂狗并保留受信维护入口 |
| P0 | 看门狗有效性，§18 | `PLATFORM_WATCHDOG_ENABLE` 默认为 0；Therapy ACK 接收可阻塞 120 秒 | 启用并落实启动/试运行健康超时；改造长阻塞操作，使看门狗开启后升级路径仍可完成 |
| P1 | 错误与日志，§19 | 已有安装阶段错误枚举，但不持久化；恢复和 Therapy 协议错误存在合并 | 补 Runtime、Trial、Rollback 等类别，保存 last_error；日志输出状态、source、组件、计数及底层原因 |
| P1 | 路径与解析边界，§20 | 多处 `snprintf()` 只检查 `<= 0`；最低版本字符串使用 `strtoul()`，未检查 ERANGE | 同时检查返回长度是否达到缓冲区容量；完整处理数字溢出；保持固定缓冲区和地址范围检查 |
| P1 | Journal 物理布局，§6.5 | A/B 已实现，但槽距为结构体长度 52 字节，两个槽共享 EEPROM 写页 | 为新 ABI 分配固定、独立且按 AT24 写页对齐的槽空间，补断电、CRC、序列号和读写失败验证 |
| P1 | 架构与构建清理，§4、21 | 基本分层已完成；Application 仍组织旧升级分支；构建仍含 mailbox/ECDSA | 状态策略集中到 Services；清理废弃接口、源文件、链接脚本用途和第三方加密构建依赖；不要修改 CURRENT 中的 firmware 原版业务代码 |

主要证据文件：

- [boot_flow.c](../Application/src/boot_flow.c)：`BootFlow_Run()` 组织恢复、mailbox 安装和普通启动。
- [update_service.c](../Services/Update/src/update_service.c)：`recover_mounted()`、`UpdateService_RecoverInterrupted()`、`UpdateService_Install()`。
- [update_internal.h](../Services/Update/src/update_internal.h) 与 [update_journal.c](../Services/Update/src/update_journal.c)：旧 Journal 数据结构及双槽读写。
- [current_store.c](../Services/Update/src/current_store.c)：`CurrentStore_Commit()`、`CurrentStore_Reconcile()` 和全包恢复。
- [package_reader.c](../Services/Update/src/package_reader.c) 与 [manifest_verify.c](../Services/Update/src/manifest_verify.c)：原始摘要校验和仍在执行的签名校验。
- [platform_config.h](../Platform/STM32H7/include/platform/platform_config.h)、[platform_therapy.c](../Platform/STM32H7/src/platform_therapy.c)、[bsp_therapy_uart.c](../BSP/src/bsp_therapy_uart.c)：看门狗默认值与阻塞 UART 等待。

**3. Journal 与状态机需要如何调整**

保留现有 CRC32、A/B 轮换、较新 sequence 选择和写后回读机制，替换其数据内容和业务接口。

1. 将跨 APP/Bootloader 的记录定义放到 `Shared/include/firmware/`；使用固定宽度整数，明确字节序、字段偏移、CRC 覆盖范围、槽地址和格式版本，并做编译期检查。
2. 按基线建议字段计算，新记录为 112 字节；该长度只适用于采纳原建议字段的情况，最终以冻结后的 ABI 为准。当前 AT24C128 写页为 64 字节，可考虑每槽预留 128 字节并页对齐，但须先核对 APP 的 EEPROM 地址分配。
3. 当前槽从偏移 64、116 开始，共享 64—127 这一写页。建议将槽分配到不同写页集合；不能仅凭“两段字节不重叠”就认定满足掉电故障隔离，需要结合 AT24 写入特性验证。
4. IDLE 必须作为有效记录返回，不能像现有 NONE 一样映射为 NOT_FOUND；否则无法识别 IDLE + Pending，也会混淆正常空闲、空白 EEPROM 和 Journal 损坏。
5. 将仅接收 phase/hash 的 `UpdateJournal_Write()` 改为提交完整记录，或通过受限状态转换接口更新。清事务字段时必须保留应持续存在的运行身份和错误信息。
6. 所有擦除前先提交 INSTALLING 和本次安装计数；每次 Trial Jump 前先提交 JUMPING 和 Jump 计数。持久化失败不得继续执行依赖该提交的操作。
7. Candidate 与 Rollback 分别计数，切 source 时按基线清零安装/Jump 计数。Candidate 达到失败上限时安装 CURRENT 声明的组件，CURRENT 未声明的目标保持原状（按文首分析假设）；Rollback 失败上限进入 FAILED。恢复范围由包内容决定，不再等同于强制恢复三类组件。
8. 遇到旧格式、两个槽均无效、I2C 不可用时不能直接当成健康 IDLE。需要定义量产初始化、已有设备迁移和无法持久化时的常驻安全行为。

建议新增 `Services/Boot/src/boot_state.c` 统一处理启动状态，并由服务向 Application 返回 Jump、Reset 或安全模式结果。`Application/src/application.c` 和 `boot_flow.c` 保持薄封装，不增加 callback 表。

普通 IDLE 必须继续保留当前快速路径：读取 Journal 后直接准备 Runtime，不挂载 SD/FatFs、不写 JUMPING、不反复要求 APP 确认。

**4. CURRENT 提交是独立事务，需要重写**

删除 `CURRENT_NEW`、`CURRENT_PREVIOUS` 宏及目录轮换逻辑。当前实际旧目录名为 `/CURRENT.previous`，迁移清理时不能只搜索 `/CURRENT_PREVIOUS` 字符串。

新流程需要满足以下不变量：

- Candidate 确认前，旧 CURRENT 完整保留。
- Candidate 确认后，先完整验证 UPDATE 与 Journal 候选摘要匹配，再删除旧 CURRENT、创建并原样复制 firmware 包到新 CURRENT、sync、完整验证新 CURRENT。“完整验证”是验证 Manifest 声明的全部内容，允许声明的组件集合只有一项或两项。
- 新 CURRENT 验证完成前，不删除 UPDATE、不清 Pending。
- 提交失败保留 Pending 和可用源，持久化提交次数/错误；不能复用当前 `recover_mounted()`，把已确认 Candidate 的提交失败当成需要立即重装 Runtime 的故障。
- Rollback 确认后清理失败 UPDATE，绝不能把它提交成 CURRENT。

必须单独实现的掉电分支：**UPDATE 已部分或全部删除，但 Pending 仍存在。** 此时应先检查 CURRENT 的原始 Manifest 摘要是否等于该候选事务的摘要，并完整验证组件和文件集合；验证成功后才能完成剩余清理并清 Pending。仅检查 CURRENT “自身合法”不足以证明它是本次已确认的新版本。

若 UPDATE 有效而 CURRENT 缺少 Manifest 声明的文件或校验不通过，可清理 CURRENT 后重新复制。若两者均不可用于完成提交，应保留可诊断状态并执行明确的提交失败策略，不能误清 Pending。基线 §15.1 的“UPDATE 必须有效”需要为 §15.3 的删除后恢复场景明确例外。

按文首分析口径，部分 UPDATE 提交后，CURRENT 仍保存该原始部分 firmware 包，不额外拼入旧组件、不改写 Manifest。未包含组件的 Runtime 保持原状，后续回滚也不处理 CURRENT 未包含的目标；删除备份目录中的旧文件不等于擦除对应 Runtime。CURRENT 因此表示可恢复的已确认包，而不一定是三个 Runtime 组件的完整快照。

保留并复用 `copy_file()` 的定长分块、目标 sync 和关闭逻辑；将 `CurrentStore_Reconcile()` 改为基于 Journal 事务身份的恢复，不再靠旧三个目录猜测提交进度。

**5. 原版 firmware、请求记录与确认协议**

CURRENT 中的 firmware 原版内容保持原样；缺少的升级请求文档作为独立协议文档补充，不放入 firmware 包。按基线，请求和状态放在 AT24 Journal。以下确认协议沿用基线要求，业务 APP 是否已经具备对应能力需要从其工程核对，不能由 CURRENT 内容原样保存推断：

- 升级导入：完成签名及 Manifest 声明的全部组件验证 → 确认 IDLE 且无 Pending → 写入完整的候选包（允许部分组件）→ sync → 从 NVM 回读原始 Manifest 摘要 → 最后提交 REQUESTED 并回读 → 复位。
- 健康确认：基线仍要求在 SDRAM、RTOS、关键任务、配置和必要自检达到健康条件后，通过受限接口确认 JUMPING 及运行身份。这里只核对现有 APP 能力，不重写 CURRENT 中保存的固件，也不擅自用“安装成功”代替健康确认。
- Candidate 确认写 IDLE + Pending；Rollback 确认写 IDLE 且不置 Pending；均需原子提交后复位。
- APP 试运行喂狗由健康管理统一控制；无关任务不能持续喂狗掩盖未启动成功。
- Pending 未完成时禁止再次导入升级；不向业务代码暴露随意修改 Journal 状态/计数的通用写接口。

若后续核实原版 APP 确实没有健康确认能力，再明确它与基线 §14 的兼容处理；当前没有证据将“改旧 APP”列为确定变更。部分包没有 APP 组件时，继续运行的 APP 如何确认本次 GUI/Therapy 包的健康，也属于运行确认协议问题，不应通过修改原始 firmware 包解决。

**6. 安装、完整性与底层可靠性补齐**

以下能力已有实现基础，应保留：

- `PackageReader_Validate()` 已对原始 Manifest 字节计算 SHA-256，并用 32 字节 `memcmp()` 对比预期值。需要把预期值来源从 mailbox 改为 Journal，覆盖 Candidate、Rollback 及提交恢复的正确事务身份。
- 文件集合通过目录枚举和 `strcmp()` 精确比较，拒绝额外文件、子目录和缺失文件；FatFs 路径查找本身不区分大小写，所以必须保留枚举检查，不能只依赖 `Stat/OpenRead`。
- Manifest 解析已有字段/重复组件/产品/硬件/大小/格式和最低 Bootloader 版本检查。
- `ImageInstaller_Install()` 已完成三类组件的分块 SHA-256、写入后立即 readback 和 `memcmp()`，并集中关闭文件及 Therapy 会话。
- `CurrentStore_Restore()` 已遍历 CURRENT 声明的所有组件；可重构复用安装执行部分，但失败策略和持久状态应由统一状态机处理。
- SPI NOR 与 AT24 的操作轮询、文件复制、组件哈希和安装循环已有喂狗调用。

需要具体修正：

1. **移除 Bootloader 签名链路。** 从包验证中移除 `UpdateManifest_Digest()` 的 canonical 签名用途和 `ManifestVerify_Verify()`；删除 Bootloader 的静态公钥配置、签名错误分支、ECDSA 适配及其构建依赖。当前公钥是未配置的占位值，会使签名路径拒绝包，不应为遵循新基线继续补公钥。
2. **精简第三方构建。** `Utilities/Crypto/CMakeLists.txt` 仍编入 `ecdsa_p256.c`；`Middlewares/Third_Party/CMakeLists.txt` 和 `firmware_mbedtls_config.h` 仍启用 ECDSA/ECP/bignum/base64/ASN.1。Bootloader 保留 SHA-256 所需部分即可，也消除现有非对称密码路径对动态分配的依赖。保留签名字段的结构解析不等于继续验证签名。
3. **允许部分组件包。** 当前 `UpdateManifest_ValidateTarget()` 要求组件掩码为 7，需要改为接受 APP/GUI/Therapy 的任意非空合法子集，同时保留未知组件、重复组件、文件名、格式、大小、范围和摘要检查。UPDATE 安装遍历 UPDATE Manifest；回滚按文首假设遍历 CURRENT Manifest。某 Manifest 未声明的组件可以缺席，但已声明文件缺失仍须拒绝，不能在安装中静默跳过。`ImageInstaller_Install()` 已按单组件执行，主要修改包验证和上层流程即可。当前 signing 对象的结构要求可保持，移除验签不要求改写原始 Manifest。
4. **修复路径截断判断。** `package_reader.c`、`image_installer.c`、`current_store.c` 多处只检查 `snprintf() <= 0`，还需拒绝返回值大于等于目标容量。当前固定短根目录降低了触发概率，但没有满足基线的检查要求。
5. **修复版本字符串溢出。** `ReadVersionValue()` 在目标平台 `unsigned long` 为 32 位时，`strtoul()` 溢出可返回 UINT32_MAX，现有 `> UINT32_MAX` 检查无法识别；应检查 ERANGE 或改为逐位有界解析。
6. **处理底层等待。** Therapy 擦除 ACK 经 ROM Boot driver → Platform → BSP 进入单次 `HAL_UART_Receive(..., 120000)`，现有分段 Delay 喂狗覆盖不到此等待。应改为有总超时的短周期接收/轮询并保留已接收进度。
7. **处理 SD 阻塞。** `FATFS/Target/sd_diskio.c` 的读写完成等待是无超时空循环，底层 HAL 读写也可能长时间阻塞；需增加明确超时、错误返回和适当的喂狗机制。不能只在外层 4 KiB 复制结束后喂狗。
8. **处理删除与错误传播。** `PlatformStorage_RemoveTree()` 当前未在循环中喂狗，目录句柄池只有 2 个；需要使无效包清理和重复清理行为有界。`current_store.c` 的 `path_exists()` 将所有 Stat 错误折叠成“不存在”，不能用这种判断决定事务源已经删除或可安全清理。
9. **校验持久化边界。** `PlatformStorage_Sync()` 已调用 `f_sync()`；SD 的 `CTRL_SYNC` 当前直接返回成功，应结合同步写实现和实卡确认落盘语义。FAT 元数据更新和 EEPROM Journal 提交不具备跨介质原子性，掉电试验须覆盖实际目录/文件删除与复制过程。

**7. Runtime、FAILED 与诊断**

`RuntimeImage_Prepare()` 已检查 MSP 8 字节对齐、SRAM 范围、Thumb 位和 Reset Handler 的 APP 区间。`PlatformCpu_IsValidStackPointer()` 使用 `<= END`，**已经允许 `0x20020000`**，不应把这一项列为尚未实现。

`PlatformCpu_Jump()` 已集中执行中断屏蔽、SysTick/NVIC 清理、Cache 维护、VTOR/控制寄存器/MSP 设置和屏障，可继续复用。需完成两项验证：Cache 保持开启或关闭的最终策略；修改 MSP 后的编译产物是否仍访问旧 C 栈帧，必要时使用受控汇编尾跳转。当前向量地址满足对齐，但平台定义的 128 字节对齐常量仍应按 H743 实际向量表约束核对。

新增状态机应明确向量无效时的动作：Candidate Trial 无效应进入规定的失败/回滚路径，Rollback 无效应在有限处理后 FAILED；不能在不增加计数的情况下反复复位。

当前 `Core/Src/main.c` 的 `Error_Handler()` 关中断后空转，没有升级错误持久化、日志维护入口或喂狗。看门狗开启后，该路径可能重复复位并再次执行旧恢复逻辑。应增加业务级 FAILED 安全循环；对于 AT24 本身故障导致无法持久化的情况，也应停止破坏性操作并驻留可诊断状态。

扩展 `update_failure_t`：保留已有擦除/写入/readback/hash 分类，补充 Runtime 向量、未确认 Trial、Candidate 回滚失败、Rollback 安装/运行失败等。`CurrentStore_Restore()` 当前丢失具体安装阶段，`recover_mounted()` 合并为 RECOVERY；Therapy 的 NACK/协议错误也合并为 IO_ERROR，需要在状态或结构化诊断中保留可区分原因。

**8. 文件调整范围**

| 操作 | 范围 |
|---|---|
| 新增 | 升级请求协议文档；共享 Journal ABI/受限协议头；`Services/Boot` 的状态机与健康确认接口；状态机与故障注入验证设施 |
| 主要重写 | `Services/Update/src/update_service.c`、`current_store.c`；Journal 模型及读写接口；`Application/src/boot_flow.c` |
| 局部调整 | `package_reader.c`、`update_manifest.c`、`version_policy.c`、`image_installer.c`、`update_types.h`、`update_service.h`；FAILED 主循环及日志；Platform/BSP 长等待 |
| 删除或移出 Bootloader 构建 | `boot_mailbox.c/.h`、`platform_retained_memory.c/.h`、旧 `boot_request.h`、`manifest_verify.c`、无其他用途的 canonical 签名工具与 ECDSA 适配 |
| 同步清理 | Services/Boot、Services/Update、Platform、Utilities/Crypto、第三方 CMake；`update_config.h` 目录/公钥宏；mbedTLS 配置；链接脚本 `.noinit` mailbox 用途 |
| 保留复用 | AT24/SPI NOR/STM32 ROM Boot driver 及其 port；原始 SHA-256/CRC32；文件存储抽象；安装块校验；Runtime 校验和集中 Jump |

检索当前源代码未发现基线建议删除的 `boot_manager_io_t`、`update_manager_port_t`、`current_manager_port_t`、`recovery_manager_t`、`image_installer_port_t`、旧 `BootControl_t`、`boot_update_request.json` 解析或重复 `Services/stm32isp`。这些不应再作为现存重构工作计入。

Application/Services 目前未直接包含 HAL、FatFs、BSP 或设备 Driver 头文件，也没有上述 Service callback 表；分层主要需要保持并收拢启动策略。设备驱动边界的函数指针 port 符合基线，不应一并删除。

**9. 实现前应补齐的设计约定**

基线 §24 已列出未决项，以下事项直接影响实现和 ABI，建议优先形成明确结论：

| 事项 | 需要明确的原因 |
|---|---|
| Candidate/Rollback 安装与 Trial 的最大次数 | 可先按建议 3 次设计，但“建议值”不能误写为已冻结产品约定 |
| APP 健康条件、最长时间和确认后复位 | §14 要求确认后复位，§24 又将 Candidate 是否固定复位列为待确认；需统一表述并定义看门狗交接 |
| Commit 连续失败策略 | 默认不能擅自启用继续运行 Runtime 的可用性例外；选择驻留或有限重试时须避免无界复位循环 |
| 量产 CURRENT 与旧设备迁移 | 初始包、Journal 初始化、旧 ABI 地址迁移、旧 APP 确认兼容性需要一起设计 |
| 部分组件包的回滚范围 | 已允许部分包；当前按 CURRENT Manifest 声明范围恢复、未声明目标不操作理解，待澄清回复。CURRENT 保存原始包，不要求拼接成三组件快照 |
| Journal 版本字段编码 | 当前 release 为四个 uint32_t，建议记录中的 candidate_version 只有一个 uint32_t；必须定义映射或调整共享布局，并检查与 Manifest 的一致性 |
| candidate/running 摘要生命周期 | 明确候选安装、回滚选择、健康确认、提交完成和清事务时各字段含义；CURRENT/回滚验证需有正确预期摘要 |
| Rollback 确认后的 UPDATE 清理 | IDLE 无 Pending 又要求普通启动不挂 NVM；需保留一次性可识别的 source/事务信息或清理标志，完成清理后恢复普通 IDLE |
| Journal 均无效或向量无效 | 基线没有完整给出这些异常的转换表；需明确安全驻留、恢复权限与错误保存方式 |
| FAILED 救援、Cache、普通 Boot Health | 沿用 §24 的待评审状态；回滚范围按部分包澄清更新，不再无条件要求三组件齐全；不要借实现增加普通启动健康机制或确认后历史版本恢复 |

**10. 建议实施顺序与验收**

1. 冻结共享协议：五状态、字段、地址、版本、计数及 APP 兼容规则；补全上述异常转换。
2. 实现新 Journal 及受限请求/确认接口，去除 mailbox 依赖；核对已有 APP 协议，保持 CURRENT 中 firmware 原版内容不变。
3. 允许非空部分组件包；实现状态机、安装/Jump 计数、按 CURRENT Manifest 范围回滚和稳定 FAILED，保留普通 IDLE 快速启动。
4. 实现确认后的两目录 CURRENT 提交与所有掉电恢复分支，去除旧目录轮换。
5. 移除签名验证链路，完成包格式、路径、错误诊断及底层等待修正，再启用并验证完整看门狗流程。
6. 完成主机状态机故障注入、目标编译与板级掉电/试运行验收。确认协议、状态机和 CURRENT 提交行为配套一致，不改写已保存的原版固件包。

未在项目自有代码中找到现成测试套件。基线 §23 的 22 项验收应逐项记录结果，不能把已有函数等同于验收通过：

| 基线验收编号 | 当前静态覆盖情况 | 需要的验证 |
|---|---|---|
| 1 | 已有不挂 NVM 的无事务快启路径，尚无新 IDLE 语义 | 新 Journal 下不写 JUMPING、不触发二次确认、不挂载 NVM |
| 2、7、17、22 | 新请求和 APP 确认协议缺失 | APP 导入/确认时序、身份错误拒绝、健康门限、Pending 阻止新升级 |
| 3、4 | 原始摘要和精确文件集合检查已有基础 | 摘要错误、缺失/额外文件、大小写、子目录均在擦除前拒绝 |
| 5、6、12、13 | 安装与 Trial 状态/计数不符合基线 | 安装掉电从头重装；Candidate 安装或 Jump 达上限进入 Rollback |
| 8、9、10、11 | 旧目录轮换不符合新提交流程 | CURRENT 删除/复制/验证/UPDATE 部分删除或全删/清 Pending 前后逐点掉电 |
| 14、15、16、21 | 缺少 Rollback Trial 和持久 FAILED；部分包须明确恢复范围 | 回滚确认保留原 CURRENT；CURRENT 未声明目标不擦除、不写入；失败达到上限常驻 FAILED，不自动 Jump/擦写 |
| 18 | A/B/CRC/回读已有基础 | 单槽任意页写入中断、另一槽恢复、双方无效、sequence 回绕、回读失败 |
| 19、20 | MSP 末端、Thumb 和范围检查已有代码 | 主机边界验证及板级 Jump；Cache 和编译后切栈序列另行验证 |

另外必须覆盖：CURRENT 缺失时禁止首次破坏性擦除、旧格式 Journal 迁移、确认后的 Commit 失败不误触发旧恢复逻辑、Therapy 长擦除和 SD 异常等待在看门狗开启时仍按有限策略退出。

部分包专项验收（按文首分析假设）：单组件/双组件包能够通过验证；UPDATE 安装和 CURRENT 回滚分别只操作各自 Manifest 声明的目标；已声明文件缺失仍拒绝；CURRENT 原始 Manifest 和组件内容与提交源逐字节一致且不含请求文件；CURRENT 缺少某目标恢复源时不将该目标擦除或误报为已恢复。若某目标已被 Candidate 损坏而 CURRENT 不提供该目标的恢复源，跳过回滚不代表故障已经解除，最终健康/FAILED 判定仍需成立。
