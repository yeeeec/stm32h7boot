# STM32H7 Bootloader 业务规则基线

> 状态：v0.9 基线。文件名为兼容旧链接保留；请求文件不再承担可信认证。

本文确定 Bootloader 业务行为与不变量；Application/Services 的代码边界见架构实施规范。

## 1. 工程与存储约束

- MCU：STM32H743；Bootloader 从 `0x08000000` 启动，分区目标为 256 KiB；
- 当前升级介质：SD 卡 + SDMMC + FatFs；未来 eMMC 只替换 Package Source Adapter；
- USB Host MSC 不作为当前升级来源；当前无强制升级按键；
- 外部 Flash：32 MiB W25Q256，256-byte page program，4 KiB erase；
- APP 从 QSPI Memory-Mapped/XIP 执行，不复制到内部 Flash；
- 不使用动态内存；长操作必须有界、增量执行；看门狗由 Platform 主循环统一维护。

固定外部 Flash 布局：

| 槽位 | Flash 偏移 | CPU 地址 | 大小 | 结束偏移（不含） |
|---|---:|---:|---:|---:|
| APP1 | `0x000000` | `0x90000000` | 1 MiB | `0x100000` |
| APP2 | `0x100000` | `0x90100000` | 1 MiB | `0x200000` |
| GUI1 | `0x200000` | `0x90200000` | 8 MiB | `0xA00000` |
| GUI2 | `0xA00000` | `0x90A00000` | 8 MiB | `0x1200000` |

`0x1200000..0x2000000` 的 14 MiB 保留，不是第三槽。QSPI `FlashSize` 应按 32 MiB 核对为 24（地址位数减一）。

## 2. 成对交替激活

APP1+GUI1 和 APP2+GUI2 是两组不可拆分发布对：

```text
读取当前 Active Record
-> 选择另一组非激活对
-> 校验源文件
-> 写入并校验目标 APP
-> 写入并校验目标 GUI
-> 生成未提交 Active Record 候选
-> Application 原子提交候选
-> Application 尝试清理请求并卸载
-> Application 请求系统复位
```

必须保持：

- 永远不擦写当前激活对；
- APP 和 GUI 均通过目标 CRC 后才能生成成功候选；
- EEPROM Active Record 是激活提交的唯一持久化确认点；
- 请求文件不是成功记录，也不是可信凭据；
- APP/GUI 不允许部分切换或跨编号配对；
- 新 Active Record 提交成功后统一系统复位，不由 Update Service 直接跳转。

## 3. 请求文件语义

固定布局：

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

请求规则：

1. 文件不存在表示不尝试常规升级；
2. 文件存在表示尝试处理固定发布目录；
3. Bootloader 不读取或解析请求内容；
4. 请求不携带包身份、目标槽、路径、状态、nonce 或签名；
5. 请求不提供来源认证、发布授权、Manifest 绑定或防篡改；
6. SD/FAT 可被修改，Manifest 和组件 SHA 只提供完整性检查；
7. 发布方必须先完整写入并关闭三个发布文件，最后创建请求；
8. 请求删除只能发生在新 Active Record 提交成功后；相同发布包的陈旧请求可直接清理；
9. 请求删除失败不回退新激活对，也不得造成相同包再次擦写。

Bootloader 不执行 Manifest ECDSA 验签，不持有公钥，不实现 Key ID、签名 Canonicalization 或密钥轮换。

## 4. 顶层模式与优先级

Application 的主要业务状态为：

| 状态 | 含义 | 主要出口 |
|---|---|---|
| `STARTUP` | 初始化、读取 Active Record | `UPDATE_CHECK`、`RECOVERY`、`FAULT` |
| `UPDATE_CHECK` | 有界探测介质和请求 | `UPDATE`、`VALIDATE_ACTIVE` |
| `UPDATE` | Prepare、策略决策、Install、Commit、Cleanup | `RESET`、`VALIDATE_ACTIVE` |
| `VALIDATE_ACTIVE` | 校验当前激活对 | `LAUNCH`、`RECOVERY` |
| `RECOVERY` | 扫描并验证保留候选，由 Application 提交 | `VALIDATE_ACTIVE`、`FAULT` |
| `RESET` | 新升级提交后的系统复位 | 不返回 |
| `LAUNCH` | 启动交接 | 不返回 |
| `FAULT` | 不启动未知镜像 | 受控诊断/复位 |

决策优先级：

1. 读取 EEPROM 最新有效 Active Record；无法正常选择时进入 Recovery；
2. 有有效 Active Record 时只对升级介质做一次有界探测；
3. 有请求时进入 Prepare；无请求或介质不可用时校验当前激活对；
4. Prepare 成功后先判断相同发布包，再判断最低 Bootloader 版本和严格升级版本；
5. 只有策略接受才开始 Install；
6. Install/Commit 失败时保留并校验旧激活对；
7. 当前激活对校验失败时进入 Recovery；
8. Recovery 无有效候选或恢复后再次校验失败时进入故障路径。

正常启动不得无界等待 SD 卡。

## 5. 发布包与策略规则

一个发布包至少包含：

- 格式版本、产品/硬件兼容标识；
- APP/GUI 共同发布版本和 `package_id`；
- 最低 Bootloader 版本；
- APPX 文件大小、SHA-256、源/目标 CRC 和重定位信息；
- GUI 文件大小、SHA-256 和 CRC；
- APPX、精简重定位表和 GUI 数据。

Manifest 中存在的签名相关字段不在 Bootloader 安全语义内，Manifest Service 不解析为认证结果。

Application 负责：

- 当前 Bootloader 版本满足 `minimum_bootloader_version`；
- 新版本严格高于当前 Active Record 版本；
- 通过 `package_id_hash128 + manifest_sha256` 判断相同发布包。

Update/Manifest Service 负责：

- 严格 Schema、产品/硬件和数值边界；
- APP/GUI 完整源文件 SHA-256；
- APPX Header、重定位表和目标地址；
- 目标 APP/GUI CRC。

低版本、同版本但非相同发布包默认拒绝。工厂降级或解锁不属于当前基线。

## 6. 安装事务

固定顺序：

```text
Application 挂载并探测请求
-> Update Prepare：读取 Manifest、计算 SHA-256、严格解析
-> Application：陈旧/最低 Bootloader/升级版本决策
-> Update Install：校验 APP/GUI 源文件和 APPX 元数据
-> 选择非激活目标对
-> 擦写并校验目标 APP
-> 擦写并校验目标 GUI
-> Update 返回未提交 Active Record 候选
-> Application 调用 Boot Control 原子提交
-> Application 尝试删除请求并卸载
-> Application 调用 system_reset
```

事务不变量：

- 源文件 SHA、APPX Header、重定位表全部通过前不得擦除目标 APP；
- APP 目标 CRC 未通过不得开始 GUI 更新；
- GUI 目标 CRC 未通过不得提交 Active Record；
- 擦除开始后取消也不得把残缺目标对激活；
- 每次 `Process()` 只执行一个有界步骤；
- 复位发生在提交前，旧 Active Record 继续有效；
- Active Record 提交是 Application 的动作，不是 Update Service 的动作；
- 请求清理是提交后的尽力操作，不是事务提交点；
- 新提交成功后不直接 Launch，统一 Reset。

## 7. 陈旧请求

Active Record 保存当前发布包的 `package_id_hash128` 与完整原始 Manifest 的 `manifest_sha256`。

两者都相等时：

- Application 不调用 Install；
- 不擦除 APP/GUI；
- 尝试删除请求并卸载；
- 删除失败时仍校验并启动当前激活对；
- 不因请求仍存在反复复位。

版本策略拒绝的请求同样不得进入 Install，因此不会重复擦写。

## 8. EEPROM Active Record

EEPROM 只保存两个 256-byte Active Record：

- A：`0x0000..0x00FF`；
- B：`0x0100..0x01FF`；
- `0x0200` 以后为保留区，不保存 Update Request。

记录包含 Magic、格式/长度、序列号、有效状态、激活对、发布版本、build、APP/GUI 大小与 CRC、`package_id_hash128`、`manifest_sha256`、记录 CRC 和 Commit Marker。

Boot Control Service 提供结构校验、A/B 选择、RFC 1982 序列比较和原子提交机制。Application 决定何时提交。

提交顺序：先使目标 Marker 无效，分页写记录主体，读回校验，最后写 Commit Marker，再最终读回校验。任何中途掉电都只能得到旧记录或完整新记录。

EEPROM 不保存：请求标志、请求状态、升级阶段、文件路径、任意跳转地址、逐块进度或 Manifest 签名结果。

## 9. 恢复规则

- Boot Control Service 可分别读取 A/B 中针对固定 pair 的结构有效候选；
- Recovery Service 通过注入的候选加载器扫描 pair 1/2，调用镜像校验能力并返回一个未提交候选；
- Recovery Service 不写 EEPROM、不决定启动或复位；
- Application 取得候选后调用 Boot Control 的恢复提交，提交时保留候选来源副本、覆盖另一副本，再次校验并启动；
- 当前 Active Record 对应镜像失败时允许进入一次 Recovery；恢复后再次校验失败则不得循环恢复或启动未知镜像；
- 两组都无有效候选或序列无法唯一选择时进入故障路径。

Recovery 只能基于 Active Record A/B 中保留的完整元数据恢复；不得从镜像内容猜测版本、长度或 CRC。

## 10. 启动规则

进入 Launch 前必须：

1. 校验 Active Record 和固定 pair 布局；
2. 按记录长度重算 APP/GUI CRC；
3. 从固定映射表选择 `0x90000000` 或 `0x90100000`；
4. 配置 W25Q256 4-byte 地址和 QSPI Memory-Mapped；
5. 复核 MSP、Thumb Reset Handler 和地址范围；
6. 按交接契约处理中断、Cache、MPU、SysTick、时钟和外设所有权；
7. 设置 VTOR/MSP 并跳转；跳转返回视为故障。

APP 使用槽位无关规范基址构建，发布工具生成排序后的精简重定位表；Bootloader 安装时按目标 XIP 基址流式重定位。Bootloader 不解析通用 ELF。

## 11. 错误决策

| 错误 | 提交前 | 提交后 |
|---|---|---|
| 介质/挂载/请求探测失败 | 校验并启动旧激活对 | 新记录不依赖介质 |
| Manifest/文件/Hash/格式/版本失败 | 不擦写或不提交，使用旧对 | 不适用 |
| APP/GUI 擦写或目标 CRC 失败 | 旧记录不变 | 不适用 |
| EEPROM 提交失败 | 使用上一有效记录 | A/B 选择最终有效记录 |
| 请求删除失败 | 不适用 | 保持新记录，复位；下次按陈旧请求处理 |
| 当前激活镜像失败 | 进入 Recovery | 进入 Recovery |
| 无恢复候选 | 进入故障路径 | 进入故障路径 |

HAL、FatFs、I2C、SDMMC 和 QSPI 原生错误仅用于日志；Application 依据稳定状态与错误类别决策。

## 12. 冻结参数与范围外能力

已冻结：32 MiB W25Q256、APP 1 MiB×2、GUI 8 MiB×2、固定槽地址、XIP、安装时重定位、固定文件路径、SHA-256、Active Record A/B、严格升级、正常启动有界介质探测。

仍需真机/产品冻结：QSPI `FlashSize=24`、CRC 参数与输入字节序、无有效镜像时的产品级故障/恢复交互。

当前范围外：网络/UART 升级、多产品组件包、外设级联升级、工厂降级/解锁、加密包、Bootloader 自更新、RTOS 调度、逐块升级进度持久化和 Trial/Confirm 启动。
