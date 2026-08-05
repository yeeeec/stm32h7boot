# STM32H7 Bootloader 业务规则基线

> 状态：基线草案 v0.6  
> 目标平台：STM32H743  
> 本文确定 Bootloader 的业务行为和安全不变量，不规定 Application/Services 的代码组织。

## 1. 已知工程约束

以下内容由当前工程直接确认：

1. MCU 内部 Flash 容量为 2 MiB；
2. 当前 Bootloader 从 `0x08000000` 启动；
3. 工程已经启用 USB Host MSC 和 FatFs；
4. 工程已经启用 QSPI 外部 SPI NOR；
5. 外部 SPI NOR 支持运行时读取 JEDEC 容量，页编程粒度为 256 字节，擦除粒度为 4 KiB；
6. 工程已经启用 CRC 和独立看门狗；
7. 当前没有配置用于强制升级的按键 GPIO；
8. 当前链接脚本仍将全部 2 MiB 内部 Flash 分配给 Bootloader，尚未定义 Application 分区；
9. 当前 Release Bootloader 的代码和只读数据约为 48 KiB，但最终分区必须为后续升级、校验和诊断功能预留空间。
10. 外部 SPI NOR 确认为 32 MiB 的 Winbond W25Q256，地址范围为 `0x000000` 至 `0x1FFFFFF`。
11. 外部 Flash 按固定顺序划分为 APP1、APP2、GUI1、GUI2 四个槽位。
12. 当前 CubeMX `QSPI FlashSize` 配置为 25；按 STM32 HAL 的“地址位数减一”定义，32 MiB W25Q256 应核对并改为 24。间接访问仍由 JEDEC 容量检查保护，但 Memory-Mapped/XIP 前必须修正地址窗口。
13. APP1/APP2 确定使用 QSPI Memory-Mapped/XIP 直接执行，不复制到 MCU 内部 Flash。

外部 Flash 固定布局如下，所有边界均满足 4 KiB 擦除对齐：

| 槽位 | Flash 偏移 | CPU Memory-Mapped 地址 | 大小 | 结束偏移（不包含） |
|---|---:|---:|---:|---:|
| APP1 | `0x000000` | `0x90000000` | 1 MiB | `0x100000` |
| APP2 | `0x100000` | `0x90100000` | 1 MiB | `0x200000` |
| GUI1 | `0x200000` | `0x90200000` | 8 MiB | `0xA00000` |
| GUI2 | `0xA00000` | `0x90A00000` | 8 MiB | `0x1200000` |

`0x1200000` 至 `0x2000000` 的剩余 14 MiB 暂不分配给升级槽位，保留给后续产品功能。不得将其隐式当作第三个 Application 槽。

## 2. 基线方案

本项目采用 APP 和 GUI 成对交替升级模型：

```text
当前激活对 APP1 + GUI1（或 APP2 + GUI2）
       |
       v
写入另一对非激活槽位
       |
       +--> APPx 写入并 CRC 校验
       |
       +--> GUIx 写入并 CRC 校验
       |
       v
EEPROM 原子提交升级成功和激活槽
       |
       v
重新计算并使用新的 Application 跳转地址
```

业务基线为：

- APP1/APP2 和 GUI1/GUI2 是两组固定的外部 Flash 槽位；
- APP1 必须与 GUI1 配对，APP2 必须与 GUI2 配对；
- 任意时刻只有一对槽位是激活对，另一对是升级目标对；
- 升级始终写入非激活对，不得覆盖当前激活对；
- APP 和 GUI 均写入且 CRC 校验成功后，才允许提交升级成功；
- EEPROM 是激活槽、升级状态和跳转目标的持久化提交介质；
- EEPROM 提交前不得改变激活槽或跳转地址；
- EEPROM 提交成功后，Bootloader 才能从固定槽位映射表选择新的 XIP 跳转地址；
- APP 与 GUI 的升级提交必须作为一个不可分割的事务；
- 一个升级包只包含一份最新 APPX 和一份最新 GUIX，不包含旧版本镜像；
- APP 与 GUI 始终同步开发、同步发布，二者共享一个发布版本，不设置独立的 APP 版本或 GUI 版本；
- 后续 Audio 作为独立组件维护自己的版本，Audio 不改变 APP/GUI 同步发布规则；
- 不使用动态内存；
- 所有长操作必须分块执行并持续维护看门狗。

如果目标对的 APP 或 GUI 写入、读取或 CRC 校验失败，EEPROM 中的激活对保持不变，下一次启动仍使用原激活对。

## 3. Bootloader 顶层模式

Bootloader 使用以下系统语义状态：

| 状态 | 含义 | 允许的主要出口 |
|---|---|---|
| `STARTUP` | 初始化并收集复位原因 | `SELECT_MODE`、`FAULT` |
| `SELECT_MODE` | 读取 Boot Control，检查事务和镜像 | `UPDATE`、`RECOVERY`、`LAUNCH`、`FAULT` |
| `UPDATE` | 接收、验证并安装新镜像 | `LAUNCH`、`RECOVERY`、`FAULT` |
| `RECOVERY` | 恢复旧镜像或等待有效升级包 | `LAUNCH`、`UPDATE`、`FAULT` |
| `LAUNCH` | 执行跳转前检查并启动 Application | 不返回；失败进入 `FAULT` |
| `FAULT` | 保持系统可诊断且不启动未知镜像 | 复位或进入受控恢复 |

Bootloader 不设置长期 `NORMAL` 状态。正常路径的最终动作是跳转 Application。

## 4. 模式选择优先级

`SELECT_MODE` 必须按照以下顺序决策，先匹配者优先：

1. EEPROM Boot Control 双副本均无效：检查两组 APP/GUI 槽，无法恢复唯一激活对时进入 `RECOVERY`；
2. EEPROM 表明升级尚未提交：保持原激活对，按升级请求决定继续 `UPDATE` 或进入 `LAUNCH`；
3. 存在持久化升级请求：进入 `UPDATE`；
4. EEPROM 指定的激活 APP/GUI 对无效，但另一对完整有效：原子切换到另一对后进入 `LAUNCH`；
5. EEPROM 指定的激活 APP/GUI 对无效，且另一对也无效：进入 `RECOVERY`；
6. 当前激活对有效：进入 `LAUNCH`；
7. 其他无法分类的状态：进入 `FAULT`。

USB 设备插入本身不构成升级请求。正常启动路径不得为了等待 USB 而增加固定延时。

在没有按键 GPIO 的当前硬件配置下，升级请求来源限定为：

- Application 写入持久化升级请求后执行系统复位；
- Bootloader 发现两组 APP/GUI 都无效而自动进入恢复模式；
- 调试或生产工具通过后续明确设计的受控接口写入升级请求。

## 5. Application 有效性规则

只有同时满足以下条件，激活 APP/GUI 对才可以被使用：

1. APP 镜像地址和长度完全位于 EEPROM 指定的 APP1 或 APP2 槽内；
2. GUI 数据地址和长度完全位于同编号的 GUI1 或 GUI2 槽内；
3. APP 和 GUI 长度均非零，且不越过各自固定分区；
4. APP 和 GUI 的实际 CRC 分别与 EEPROM 已提交记录一致；
5. APP 和 GUI 必须属于同一个发布版本；
6. Manifest 的目标硬件标识与当前产品一致；
7. 镜像 Hash 与 Manifest 一致；
8. Manifest 签名有效；
9. 版本满足防回滚策略；
10. Boot Control 将这一对标记为激活且升级成功；
11. 最终执行映射中的向量表 MSP 和 Reset Handler 通过跳转前检查。

CRC 只能用于检测传输或存储损坏，不能替代镜像签名和安全 Hash。

## 6. 升级包规则

一个升级包至少必须包含：

- 包格式版本；
- 产品和硬件兼容标识；
- APP/GUI 共同发布版本；
- 发布包标识，用于关联 APP 和 GUI；
- APP 镜像长度和 CRC；
- GUI 数据长度和 CRC；
- APP 镜像 Hash；
- GUI 数据 Hash；
- 签名算法标识；
- 签名；
- APP 镜像数据；
- APP 重定位表；
- GUI 数据。

升级包中的 APP 和 GUI 使用逻辑名称 `APPX`、`GUIX`，不预先绑定为槽 1 或槽 2。Bootloader 根据 EEPROM 当前激活槽选择实际写入目标：

```text
当前 APP1 + GUI1 激活 -> APPX 写 APP2，GUIX 写 GUI2
当前 APP2 + GUI2 激活 -> APPX 写 APP1，GUIX 写 GUI1
```

Manifest 使用发布组表达版本关系：当前 `app-gui` 发布组包含 APP 和 GUI，具有一个共同版本并原子提交。后续 Audio 使用独立的 `audio` 发布组和独立版本；在 Audio 存储分区与提交策略确定前，不纳入当前 APP/GUI 事务。

升级包处理规则：

1. 文件系统、文件名和包头检查必须先于大文件复制；
2. 所有长度和地址计算必须使用 checked arithmetic；
3. 未识别字段、重复组件、越界长度或不兼容格式必须拒绝；
4. USB 文件只作为输入源，Bootloader 不依赖修改或删除 USB 文件来防止重复升级；
5. APP 和 GUI 必须作为一个升级包处理；
6. APP 和 GUI 必须完整写入目标槽后再执行最终 CRC 校验；
7. 任一组件校验失败，整个升级事务失败，不改变 EEPROM 激活记录；
8. 同一版本只允许用于恢复同一发布包，不作为常规升级；
9. 低版本默认拒绝，工厂解锁流程不属于当前基线。

升级文件路径、Manifest 二进制布局、Hash 算法和签名算法必须在包格式规范中单独冻结。

## 7. 安装事务

升级目标槽位由当前激活对决定：

| 当前激活对 | 本次升级目标对 |
|---|---|
| APP1 + GUI1 | APP2 + GUI2 |
| APP2 + GUI2 | APP1 + GUI1 |

安装顺序固定为：

```text
检测升级请求
-> 等待 USB MSC 就绪
-> 挂载文件系统
-> 定位升级包
-> 校验包头和 Manifest
-> 根据当前激活对选择非激活 APP/GUI 目标槽
-> 分块擦除目标 APP 槽
-> 分块读取 APPX、应用目标基址重定位并写入目标 APP 槽
-> 读取目标 APP 槽并计算 CRC
-> APP CRC 正确后，分块擦除目标 GUI 槽
-> 分块写入目标 GUI 槽
-> 读取目标 GUI 槽并计算 CRC
-> APP 和 GUI CRC 均正确后，写入 EEPROM 升级成功记录
-> 读回并校验 EEPROM 记录
-> 仅此时改变激活槽和 Application 跳转目标
-> 清除升级请求
-> 复位或启动新的激活对
```

必须保持以下事务不变量：

- 升级始终写入非激活对，不得擦除当前激活 APP 或 GUI 槽；
- APP 重定位表和 APPX 原始载荷校验通过前，不得擦除目标 APP 槽；
- APP CRC 未通过前，不得开始 GUI 更新；
- GUI CRC 未通过前，不得写入 EEPROM 升级成功记录；
- EEPROM 升级成功记录写入失败或读回校验失败时，激活对和跳转目标保持不变；
- 只有 APP 和 GUI 都完成 CRC，才允许一次性提交新的激活对；
- 擦除目标槽开始后不得取消并把残缺目标对标记为激活；
- 每个 `process()` 调用最多处理一个有界数据块或一个状态转换；
- 任意时刻复位后，都能通过 EEPROM 记录判断继续升级或回到原激活对；
- APP 写成功但 GUI 写失败时，目标 APP 保持非激活，下一次升级必须重新擦写目标对；
- 不得只切换 APP 而保留另一编号的 GUI。

## 8. EEPROM 提交与交替激活

EEPROM 是升级提交的唯一业务确认点。建议使用两个固定记录位置，避免单次 EEPROM 写入掉电造成激活信息损坏。

一条提交记录至少包含：

- Magic、格式版本、记录长度；
- 单调递增序列号；
- 当前激活 APP 槽号和 GUI 槽号；
- APP/GUI 版本、长度和 CRC；
- 升级状态；
- Application 跳转目标或其可验证的槽位映射；
- 记录 CRC。

提交顺序如下：

1. 先完成目标 APP 和 GUI 的写入及 CRC；
2. 生成新的激活记录，槽号从当前对切换到另一对；
3. 写入非当前 EEPROM 记录位置；
4. 读回并校验记录 CRC、序列号和槽位；
5. 校验成功后，新的记录才成为有效记录；
6. Bootloader 从有效记录计算跳转地址，不直接信任未校验的地址字段。

因此，掉电发生在 EEPROM 提交之前时，旧激活对继续有效；掉电发生在提交过程中时，使用序列号和 CRC 选择旧记录或新记录；掉电发生在提交之后时，使用新激活对。

本基线不要求 Application 再次确认升级成功。升级成功的定义就是：APP CRC、GUI CRC 和 EEPROM 提交全部成功。若后续需要试运行确认，应在新需求中增加 `TRIAL` 状态，而不能隐式改变当前规则。

## 9. 恢复规则

恢复模式按以下优先级执行：

1. EEPROM 当前激活对完整有效时，继续使用当前激活对；
2. 当前激活对无效而另一对完整有效时，将另一对原子提交为激活对；
3. EEPROM 表明升级未完成，但旧激活对有效时，忽略未提交的目标对，可重新执行升级；
4. APP 已写完但 GUI 未写完时，目标对仍然无效，不允许部分切换；
5. EEPROM 双副本无效时，分别检查 APP1+GUI1 和 APP2+GUI2，只有能够确定一组完整有效发布包时才能重建记录；
6. 两组都无效或无法确定唯一有效组时，等待 USB MSC 提供有效升级包；
7. USB 断开、文件缺失或包无效时保持在恢复模式，不跳转未知 APP；
8. 恢复模式必须持续喂狗，并允许输出诊断信息。

## 10. Boot Control 业务状态

Boot Control 至少表示以下状态：

| 状态 | 含义 |
|---|---|
| `EMPTY` | 尚无可信记录，需检查实际镜像 |
| `ACTIVE_VALID` | EEPROM 指定的 APP/GUI 对已提交并有效 |
| `UPDATE_REQUESTED` | Application 请求进入升级模式 |
| `UPDATE_IN_PROGRESS` | 正在写非激活 APP/GUI 对，当前激活对不变 |
| `UPDATE_SUCCESS` | 目标 APP/GUI CRC 已通过，新激活对已提交 |
| `RECOVERY_REQUIRED` | 无法自动完成，需要外部升级包 |

Boot Control 存储规则：

- EEPROM 至少使用两个独立记录位置保存提交记录；
- 每条记录包含 Magic、格式版本、记录长度、单调序列号、状态、激活 APP/GUI 槽、版本、长度、组件 CRC、跳转映射和记录 CRC；
- 写新记录时先写非当前副本，读回校验成功后才视为提交；
- 启动时选择 CRC 有效且序列号最新的记录；
- 单副本损坏不得阻止系统从另一副本恢复；
- 两副本同时无效时不得直接假定 APP/GUI 有效，必须重新验证两组槽位。

## 11. 错误决策

| 错误类别 | EEPROM 新激活记录未提交 | EEPROM 新激活记录已提交 |
|---|---|---|
| USB 未连接或断开 | 旧激活对不变，可继续等待或启动旧 APP | 新激活对已生效，不依赖 USB |
| 文件缺失或格式错误 | 拒绝升级，旧激活对不变 | 不适用，提交前必须完成检查 |
| 签名、Hash 或版本失败 | 拒绝升级，旧激活对不变 | 不适用，提交前必须完成检查 |
| 目标 APP 写入或 CRC 失败 | 整体失败，旧激活对不变 | 不适用 |
| 目标 GUI 写入或 CRC 失败 | 整体失败，旧激活对不变 | 不适用 |
| EEPROM 写入或读回失败 | 使用上一条有效记录，旧激活对不变 | 使用 CRC 有效且序列号最新的记录 |
| 当前激活对 CRC 失败 | 检查并切换到另一完整有效对 | 检查并切换到另一完整有效对 |
| Boot Control 单副本损坏 | 使用有效副本并修复 | 使用有效副本并修复 |
| Boot Control 双副本损坏 | 完整验证两组槽位并重建记录 | 完整验证两组槽位并重建记录 |
| 内部不可分类错误 | 不改变激活记录 | 进入 `FAULT` 或恢复 |

底层 HAL、FatFs、USBH 和 QSPI 原生错误只能用于日志。Application 只能依据稳定的 Service 错误类别进行以上决策。

## 12. 启动 Application 规则

进入 `LAUNCH` 后必须：

1. 读取 EEPROM 最新有效记录并确定激活 APP/GUI 对；
2. 重新校验激活 APP 和 GUI 的槽位边界及 CRC；
3. 根据激活 APP 槽从固定表选择 `0x90000000` 或 `0x90100000`；
4. 将 W25Q256 配置为 4 字节地址模式并进入 QSPI Memory-Mapped 读模式；
5. 再次检查 Memory-Mapped Application 向量表；
6. 停止 Bootloader 拥有的周期操作；
7. 关闭或复位 Bootloader 使用且不能移交的外设，但保持 QSPI Memory-Mapped 执行环境；
8. 禁止中断并清除待处理中断；
9. 按约定配置 Cache、MPU、SysTick 和时钟所有权；
10. 设置 `SCB->VTOR` 为激活 APP 槽基址；
11. 设置 MSP 并跳转 Reset Handler；
12. 跳转函数若返回，立即进入 `FAULT`。

Bootloader 与 Application 必须共享一份明确的启动交接契约，不能依赖 Application 猜测 Bootloader 留下的外设状态。

QSPI XIP 必须满足以下规则：

- `APP1` 重定位后的向量表和运行基址固定为 `0x90000000`；
- `APP2` 重定位后的向量表和运行基址固定为 `0x90100000`；
- QSPI Memory-Mapped 窗口覆盖整个 32 MiB W25Q256；
- APP 区域在 MPU 中必须允许执行，GUI 和保留区必须禁止执行；
- APP 执行期间不得退出 Memory-Mapped 模式；
- 任何修改 QSPI 控制器或 W25Q256 模式的代码必须位于 MCU 内部 Flash/RAM，不能从正在被重配置的 QSPI 执行；
- 间接擦写目标槽前必须退出 Memory-Mapped 模式；写入完成后必须清理或失效相关 Cache，再重新进入 Memory-Mapped 模式；
- APP 和 GUI CRC 按 Manifest 中的实际长度计算，不包含槽位剩余的 `0xFF` 填充区；
- 若 APPX 在安装时执行重定位，EEPROM 保存的 APP CRC 必须基于重定位后实际写入目标槽的最终字节；包内 APPX 原始载荷使用独立的包 CRC/Hash 校验；
- EEPROM 保存激活槽号，不保存任意跳转地址；Bootloader 只允许从固定槽位表计算跳转地址。

当前 QSPI Driver/Adapter 只实现间接读写，尚需增加 Memory-Mapped 进入、退出和状态查询能力。

由于 Cortex-M 固件通常包含绝对地址，同一个普通链接生成的 APP 二进制不能同时安全运行于 APP1 和 APP2。当前业务已经确定升级包只包含一份 APPX，并且不允许包含两个链接变体，因此采用安装时重定位方案：

1. APPX 使用槽位无关的规范基址生成；
2. 构建工具从最终 ELF 提取允许的重定位项，生成排序后的精简重定位表；
3. 升级包只携带一份 APPX 和一份重定位表；
4. Bootloader 根据非激活目标槽选择 `0x90000000` 或 `0x90100000` 作为运行基址；
5. Bootloader 在分块写入 APPX 时应用重定位，生成目标槽最终可执行字节；
6. 向量表中的处理函数地址和其他内部绝对地址必须由同一重定位机制修正；
7. 重定位后的 APP 槽重新计算 CRC，CRC 通过后才允许继续 GUI 更新。

普通固定链接的裸 `.bin` 不满足该要求。仅修改向量表也不充分，因为代码中的函数指针、常量地址和全局对象地址仍可能包含链接时绝对地址。

重定位表规则如下：

- 包格式只允许项目明确支持的重定位类型；
- 每个重定位项必须按地址递增排列，目标偏移必须位于 APP 实际长度内并满足对齐要求；
- 重复、越界、未排序或未知类型的重定位项必须拒绝；
- 重定位表必须被 Manifest Hash 和签名覆盖；
- Bootloader 不解析通用 ELF，只解析固定版本的精简重定位表；
- 重定位必须支持流式分块处理，不允许将完整 1 MiB APP 载入 RAM；
- 包内 APPX 原始 Hash 用于验证输入，EEPROM 中的 APP CRC 用于验证重定位后目标槽；
- 重定位失败时目标 APP 槽保持非激活，EEPROM 激活记录不得改变。

## 13. 待冻结参数

以下参数必须在开始 Application/Service 具体实现前确认：

| 参数 | 当前建议 | 冻结依据 |
|---|---|---|
| Bootloader 分区大小 | 256 KiB | 当前约 48 KiB，需预留密码学和升级逻辑 |
| W25Q256 容量 | 32 MiB | 已确认，同时继续使用 JEDEC 运行时校验 |
| APP1/APP2 大小 | 各 1 MiB | 已确认 |
| GUI1/GUI2 大小 | 各 8 MiB | 已确认 |
| QSPI 槽位地址 | `0x000000/0x100000/0x200000/0xA00000` | 已按顺序确定 |
| APP 执行方式 | QSPI Memory-Mapped/XIP | 已确认 |
| APP1 跳转地址 | `0x90000000` | QSPI 基址加 APP1 偏移 |
| APP2 跳转地址 | `0x90100000` | QSPI 基址加 APP2 偏移 |
| 升级包 APP 数量 | 一份最新 APPX | 已确认，按非激活槽写入 |
| APP 双地址运行方式 | 安装时重定位 | 已确认不允许双链接变体 |
| 重定位表格式 | 待详细设计 | 固定版本、白名单类型、流式处理 |
| QSPI `FlashSize` | 建议从 25 改为 24 | W25Q256 为 32 MiB，XIP 前必须实测确认 |
| GUI 激活基地址传递方式 | EEPROM 槽号或固定映射 | 需与 Application 共享契约 |
| EEPROM 型号、容量和页大小 | `AT24C128AN` | 当前工程尚无 EEPROM Driver/Adapter |
| EEPROM 双记录偏移 | 双缓存记录 | 必须支持掉电安全提交 |
| 升级文件路径 | `/firmware/update.pkg` | 需确认运维流程 |
| CRC 算法参数 | 建议 CRC-32 | 多项式、初值、输入/输出反转和字节序必须冻结 |
| Hash 算法 | SHA-256 | 需选择或引入密码学实现 |
| 签名算法 | ECDSA P-256 | 需确认密钥和生产烧录流程 |
| 防回滚 | 默认启用 | 需确认版本编码和工厂解锁需求 |
| 恢复模式等待时间 | 无限等待并喂狗 | 无有效镜像时必须可维护 |
| 正常启动 USB 等待 | 0 ms | 有效 Application 应快速启动 |

在这些参数冻结前，可以设计稳定接口、状态机和 Host 测试，但不得把建议值散落为业务代码中的魔数。

## 14. 不在当前基线中的功能

以下能力暂不纳入第一阶段：

- 网络升级；
- UART 升级协议；
- 多产品组件包；
- 外部设备固件级联升级；
- 工厂降级或安全解锁流程；
- 加密固件包；
- Bootloader 自更新；
- RTOS 调度。

新增以上能力时必须修订本文，并重新评估分区、安全和掉电恢复不变量。
