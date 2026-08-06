# STM32H7 Bootloader 业务规则基线

> 状态：基线草案 v0.8  
> 目标平台：STM32H743  
> 依据：v0.7，并将可信认证边界调整为文件型升级授权请求；Bootloader 不再重复执行 Manifest 验签。  
> 本文确定 Bootloader 的业务行为和安全不变量，不规定 Application/Services 的代码组织。

## 1. 已知工程约束

以下内容由当前工程和本版决策直接确认：

1. MCU 内部 Flash 容量为 2 MiB；
2. 当前 Bootloader 从 `0x08000000` 启动；
3. 当前无法使用 eMMC，升级文件暂存于 SD 卡文件系统；
4. SD 卡通过 SDIO/SDMMC 外设接入，文件系统使用 FatFs；
5. 未来恢复 eMMC 后，只允许替换文件系统底层 Adapter，不修改 Bootloader Application、Update Service、Manifest、安装事务和提交规则；
6. USB Host MSC 不再作为当前 Bootloader 的升级文件来源；
7. 工程已经启用 QSPI 外部 SPI NOR；
8. 外部 SPI NOR 支持运行时读取 JEDEC 容量，页编程粒度为 256 字节，擦除粒度为 4 KiB；
9. 工程已经启用 CRC 和独立看门狗；
10. 当前没有配置用于强制升级的按键 GPIO；
11. 当前链接脚本仍将全部 2 MiB 内部 Flash 分配给 Bootloader，尚未定义内部 Flash Application 分区；
12. 当前 Release Bootloader 的代码和只读数据约为 48 KiB，但最终分区必须为升级、SHA-256、CRC、诊断和后续扩展预留空间；
13. 外部 SPI NOR 确认为 32 MiB 的 Winbond W25Q256，地址范围为 `0x000000` 至 `0x1FFFFFF`；
14. 外部 Flash 按固定顺序划分为 APP1、APP2、GUI1、GUI2 四个槽位；
15. 当前 CubeMX `QSPI FlashSize` 配置为 25；按 STM32 HAL 的“地址位数减一”定义，32 MiB W25Q256 应核对并改为 24；
16. APP1/APP2 确定使用 QSPI Memory-Mapped/XIP 直接执行，不复制到 MCU 内部 Flash。

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
       +--> APPX 写入并校验
       |
       +--> GUIX 写入并校验
       |
       v
EEPROM 原子提交新的 Active Record
       |
       v
清除文件系统升级请求
       |
       v
系统复位并从新的激活对启动
```

业务基线为：

- APP1/APP2 和 GUI1/GUI2 是两组固定的外部 Flash 槽位；
- APP1 必须与 GUI1 配对，APP2 必须与 GUI2 配对；
- 任意时刻只有一对槽位是激活对，另一对是升级目标对；
- 升级始终写入非激活对，不得覆盖当前激活对；
- APP 和 GUI 均写入且目标校验成功后，才允许提交升级成功；
- EEPROM Active Record 是激活槽、已提交版本和跳转目标的唯一持久化确认点；
- 文件系统请求既表达升级触发，也承担对确定 Manifest 的可信授权；它不是升级成功记录；
- EEPROM 提交前不得改变激活槽或跳转地址；
- EEPROM 提交成功后，Bootloader 才能从固定槽位映射表选择新的 XIP 跳转地址；
- APP 与 GUI 的升级提交必须作为一个不可分割的事务；
- 一个升级包只包含一份最新 APPX 和一份最新 GUIX，不包含旧版本镜像；
- APP 与 GUI 始终同步开发、同步发布，共享一个发布版本；
- 后续 Audio 作为独立组件维护自己的版本，Audio 不改变当前 APP/GUI 同步发布规则；
- 不使用动态内存；
- 所有长操作必须分块执行并持续维护看门狗；
- 升级成功后统一执行系统复位，不直接从 Update Service 跳转新 Application。

如果目标对的 APP 或 GUI 写入、读取、Hash 或 CRC 校验失败，EEPROM 中的激活对保持不变，下一次启动仍使用原激活对。

## 3. 当前人工流程与正式流程

当前验证阶段由人工模拟正式 Application 的可信授权工作：

```text
人工生成或取得完整发布文件
-> 人工确认该发布包可信
-> 人工把 manifest.json、hmi.app.bin、hmi.gui.bin 写入 /firmware
-> 人工计算完整 manifest.json 的 SHA-256
-> 人工创建 /boot_update_request.json，写入 package_id 和 manifest_sha256
-> 复位设备
-> Bootloader 按请求绑定的 Manifest 执行 SHA 完整性校验、安装、目标校验和提交
```

正式版本的目标流程为：

```text
Application 获取升级内容
-> Application 完成解密或传输处理
-> Application 使用受信公钥验证 Manifest 签名
-> Application 确认产品、硬件和发布策略允许升级
-> Application 将完整发布文件写入文件系统
-> Application 计算最终 manifest.json 的 SHA-256
-> Application 最后原子创建 /boot_update_request.json
-> Application 执行系统复位
-> Bootloader 校验 request.manifest_sha256、APP/GUI SHA-256 并执行安装
```

必须保持：

- `boot_update_request.json` 是对一个确定 Manifest 的可信升级授权，不只是普通触发标志；
- 正式模式下，Application 只有在 Manifest 验签成功后才允许创建请求文件；
- 请求通过 `manifest_sha256` 与完整 Manifest 文件绑定；
- Bootloader 不再重复验证 Manifest ECDSA 签名，也不持有 Manifest 验签公钥；
- Bootloader 必须计算 Manifest SHA-256 并与请求比较，然后计算 APP/GUI SHA-256 并与已绑定 Manifest 比较；
- Bootloader 仍负责格式、边界、版本、防回滚、槽位、APPX、重定位和目标 CRC 检查；
- 当前人工模式把操作者视为可信授权源，只适用于开发、调试或受控生产环境；
- 可移除 SD 卡上的普通 FAT 文件本身不天然具备防篡改能力。正式产品若允许不受信主体修改该文件系统，必须由存储访问控制、受保护分区或其他机制保证请求文件的可信性。

## 4. Bootloader 顶层模式

Bootloader 使用以下系统语义状态：

| 状态 | 含义 | 允许的主要出口 |
|---|---|---|
| `STARTUP` | 初始化并收集复位原因 | `SELECT_MODE`、`FAULT` |
| `SELECT_MODE` | 读取 Active Record，并有界检查文件系统升级请求 | `UPDATE`、`RECOVERY`、`LAUNCH`、`FAULT` |
| `UPDATE` | 验证并安装新镜像 | `RESET`、`RECOVERY`、`LAUNCH`、`FAULT` |
| `RECOVERY` | 恢复旧镜像或等待有效文件系统升级包 | `LAUNCH`、`UPDATE`、`FAULT` |
| `RESET` | 升级提交成功后的受控系统复位 | 不返回；失败进入 `FAULT` |
| `LAUNCH` | 执行跳转前检查并启动 Application | 不返回；失败进入 `FAULT` |
| `FAULT` | 保持系统可诊断且不启动未知镜像 | 复位或进入受控恢复 |

Bootloader 不设置长期 `NORMAL` 状态。正常路径的最终动作是跳转 Application；升级成功路径的最终动作是系统复位。

## 5. 文件系统布局与可信升级请求

文件系统根目录固定布局为：

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

路径必须由 Bootloader 只读产品配置冻结：

```text
request path  = /boot_update_request.json
package dir   = /firmware
manifest      = /firmware/manifest.json
app payload   = /firmware/hmi.app.bin
gui payload   = /firmware/hmi.gui.bin
```

请求文件 V2 的业务语义固定为：

```json
{
  "format_version": 2,
  "requested": true,
  "package_id": "hmi-release-1.2.3-20260806",
  "manifest_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

规则：

1. 请求文件不存在表示没有升级请求；
2. 请求文件存在但格式错误，不得视为有效授权；
3. `format_version` 必须为 `2`；
4. `requested` 必须为布尔值 `true`；
5. `package_id` 必须与 SHA 校验通过后的 Manifest 中 `package_id` 完全一致；
6. `manifest_sha256` 必须是完整 `manifest.json` 文件的 SHA-256，使用 64 位小写十六进制；
7. 请求文件的存在及内容表示：创建者已经完成 Manifest 可信认证，并授权 Bootloader 安装该 Manifest 标识的发布包；
8. Bootloader 必须先计算 Manifest SHA-256 并与请求比较，匹配后才允许信任 Manifest 中的 APP/GUI Hash、版本和安装元数据；
9. 请求文件不得携带升级目录、文件名、目标槽位或任意物理地址；
10. SD 卡插入或文件系统可挂载本身不构成升级请求；
11. Bootloader 不修改 `manifest.json`、`hmi.app.bin` 或 `hmi.gui.bin`；
12. Bootloader 只允许在升级提交成功后删除 `boot_update_request.json`；
13. 正式实现必须保证请求文件只能由受信 Application 或受控生产工具创建；
14. 当前人工方式是显式开发信任覆盖，不构成量产级密码学认证；
15. 请求文件的完整格式、大小和解析规则由独立文件系统升级契约冻结。

## 6. 模式选择优先级

`SELECT_MODE` 必须按照以下顺序决策，先匹配者优先：

1. 读取并选择 EEPROM 最新有效 Active Record；
2. Active Record 双副本均无效时，检查两组 APP/GUI 槽；无法恢复唯一激活对时进入 `RECOVERY`；
3. 对升级文件系统执行一次有界探测，不得在正常启动路径无限等待 SD 卡；
4. 存在合法的 `/boot_update_request.json` 时进入 `UPDATE`；
5. 请求文件存在但格式错误或无法读取时，记录稳定错误；若旧激活对有效，可按产品策略启动旧 Application；若无有效激活对，进入 `RECOVERY`；请求与 Manifest 的绑定由 Update Service 在 `manifest_sha256` 校验通过后执行；
6. Active Record 指定的激活 APP/GUI 对无效，但另一对完整有效时，原子提交另一对后进入 `LAUNCH`；
7. Active Record 指定的激活 APP/GUI 对无效，且另一对也无效时进入 `RECOVERY`；
8. 当前激活对有效且无有效升级请求时进入 `LAUNCH`；
9. 其他无法分类的状态进入 `FAULT`。

正常启动路径不得为了等待 SD 卡而增加无界延时。允许一次有超时上限的初始化、挂载和请求文件探测。

在没有按键 GPIO 的当前硬件配置下，升级请求来源限定为：

- 当前验证阶段由人工在文件系统根目录创建合法请求文件；
- 正式版本由 Application 在完整写入并关闭升级文件后原子创建请求文件，并执行系统复位；
- 调试或生产工具通过受控方式创建相同格式的请求文件；
- Bootloader 发现两组 APP/GUI 都无效时自动进入恢复模式，等待文件系统提供有效包。

## 7. Application 有效性规则

只有同时满足以下条件，激活 APP/GUI 对才可以被使用：

1. APP 镜像地址和长度完全位于 Active Record 指定的 APP1 或 APP2 槽内；
2. GUI 数据地址和长度完全位于同编号的 GUI1 或 GUI2 槽内；
3. APP 和 GUI 长度均非零，且不越过各自固定分区；
4. APP 和 GUI 的实际 CRC 分别与已提交 Active Record 一致；
5. APP 和 GUI 属于同一个发布版本；
6. 该 Active Record 只可能由合法可信请求授权、Manifest SHA 绑定、组件 Hash、产品/硬件兼容和版本策略全部通过的升级事务生成；
7. Active Record 的 Magic、格式、序列号、记录 CRC 和 Commit Marker 有效；
8. Active Record 将这一对标记为 `ACTIVE_VALID`；
9. 最终执行映射中的向量表 MSP 和 Reset Handler 通过跳转前检查。

正式 Application 的 Manifest 验签建立发布授权；请求文件把该授权绑定到确定的 Manifest SHA-256；Bootloader 的 Manifest/组件 SHA 和目标 CRC 用于检测交接后的文件或存储损坏。正常启动阶段不依赖 SD 卡中的源升级文件继续存在。

## 8. 升级包规则

一个升级包至少必须包含：

- 包格式版本；
- 产品和硬件兼容标识；
- APP/GUI 共同发布版本；
- 发布包标识 `package_id`；
- APP 镜像长度和 CRC；
- GUI 数据长度和 CRC；
- APP 镜像 Hash；
- GUI 数据 Hash；
- 签名算法标识；
- 签名；
- APP 镜像数据；
- APP 重定位表；
- GUI 数据。

升级包中的 APP 和 GUI 使用逻辑名称 `APPX`、`GUIX`，不预先绑定为槽 1 或槽 2。Bootloader 根据 Active Record 当前激活槽选择实际写入目标：

```text
当前 APP1 + GUI1 激活 -> APPX 写 APP2，GUIX 写 GUI2
当前 APP2 + GUI2 激活 -> APPX 写 APP1，GUIX 写 GUI1
```

Manifest 使用发布组表达版本关系：当前 `app-gui` 发布组包含 APP 和 GUI，具有一个共同版本并原子提交。后续 Audio 使用独立的 `audio` 发布组和独立版本；在 Audio 存储分区与提交策略确定前，不纳入当前 APP/GUI 事务。

升级包处理规则：

1. 文件系统、固定路径、文件名和包头检查必须先于大文件复制；
2. 所有长度和地址计算必须使用 checked arithmetic；
3. 未识别字段、重复组件、越界长度或不兼容格式必须拒绝；
4. 三个发布文件只作为只读安装输入；Bootloader 不修改或删除它们；
5. APP 和 GUI 必须作为一个升级包处理；
6. Bootloader 必须计算完整 Manifest SHA-256 并匹配可信请求，再独立计算 APP/GUI SHA-256 并匹配该 Manifest；
7. APP 和 GUI 必须完整写入目标槽后再执行最终 CRC 校验；
8. 任一组件校验失败，整个升级事务失败，不改变 EEPROM Active Record；
9. 同一版本只允许用于恢复同一发布包，不作为常规升级；
10. 低版本默认拒绝，工厂解锁流程不属于当前基线；
11. 防止重复安装必须同时使用 Active Record 中的 `package_id_hash128` 和 `manifest_sha256`，不得只依赖请求文件删除成功。

升级文件路径、可信请求格式、Manifest Schema 和 Hash 算法由独立规范冻结；Manifest 签名算法属于 Application/发布工具验证契约。

## 9. 安装事务

升级目标槽位由当前激活对决定：

| 当前激活对 | 本次升级目标对 |
|---|---|
| APP1 + GUI1 | APP2 + GUI2 |
| APP2 + GUI2 | APP1 + GUI1 |

安装顺序固定为：

```text
有界挂载升级文件系统
-> 读取并严格解析 /boot_update_request.json
-> 读取 /firmware/manifest.json
-> 计算完整 Manifest SHA-256
-> 校验 request.manifest_sha256 == actual_manifest_sha256
-> 严格解析已绑定 Manifest
-> 校验 request.package_id == manifest.package_id
-> 校验产品、硬件、版本和防回滚策略
-> 计算并校验 hmi.app.bin SHA-256
-> 校验 APPX Header 与重定位表
-> 计算并校验 hmi.gui.bin SHA-256
-> 检查当前 Active Record 是否已经提交相同 package_id 和 manifest_sha256
-> 若已提交，仅清理陈旧请求并复位/启动，不重复擦写
-> 根据当前激活对选择非激活 APP/GUI 目标槽
-> 分块擦除目标 APP 槽
-> 分块读取 APPX、应用目标基址重定位并写入目标 APP 槽
-> 读取目标 APP 槽并计算 CRC
-> APP CRC 正确后，分块擦除目标 GUI 槽
-> 分块写入目标 GUI 槽
-> 读取目标 GUI 槽并计算 CRC
-> APP 和 GUI CRC 均正确后，写入 EEPROM 新 Active Record
-> 读回并校验 EEPROM Active Record
-> 仅此时改变激活槽和 Application 跳转目标
-> 删除 /boot_update_request.json
-> 执行系统复位
```

必须保持以下事务不变量：

- 升级始终写入非激活对，不得擦除当前激活 APP 或 GUI 槽；
- 请求文件是本次升级的可信授权边界；未经有效请求授权不得进入常规安装；
- Manifest SHA 与请求不匹配时，必须拒绝升级；
- APP/GUI 源 SHA、APPX Header 和重定位表全部通过前，不得擦除目标 APP 槽；
- Bootloader 不执行 Manifest ECDSA 验签；
- APP CRC 未通过前，不得开始 GUI 更新；
- GUI CRC 未通过前，不得写入 EEPROM 新 Active Record；
- EEPROM Active Record 写入失败或读回校验失败时，激活对和跳转目标保持不变；
- 只有 APP 和 GUI 都完成目标 CRC，才允许一次性提交新的激活对；
- 擦除目标槽开始后不得取消并把残缺目标对标记为激活；
- 每个 `process()` 调用最多处理一个有界数据块、一个异步轮询或一个状态转换；
- 升级过程中复位时，旧 Active Record 仍有效，请求文件仍存在，下一次升级从头重写非激活对；
- APP 写成功但 GUI 写失败时，目标 APP 保持非激活，下一次升级必须重新擦写目标对；
- 不得只切换 APP 而保留另一编号的 GUI；
- 必须先提交 Active Record，再清除请求文件；
- Active Record 已提交但请求文件删除失败时，不得回退新激活对，也不得在下一次启动重复安装相同包；
- 请求文件删除成功后、系统复位前掉电时，新 Active Record 已经有效，下一次启动使用新激活对。

## 10. EEPROM Active Record 提交与交替激活

EEPROM Active Record 是升级提交的唯一业务确认点。使用两个固定记录位置，避免单次 EEPROM 写入掉电造成激活信息损坏。

一条提交记录至少包含：

- Magic、格式版本、记录长度；
- 单调递增序列号；
- `state = ACTIVE_VALID`；
- 当前激活 APP 槽号和 GUI 槽号；
- APP/GUI 共同版本、长度和 CRC；
- `package_id_hash128`；
- 可信请求授权的完整 Manifest 文件 `manifest_sha256`；
- 记录 CRC；
- 最后提交标记 Commit Marker。

提交顺序如下：

1. 先完成目标 APP 和 GUI 的写入及目标 CRC；
2. 生成新的 Active Record，槽号从当前对切换到另一对；
3. 写入非当前 EEPROM 记录位置，Commit Marker 保持无效；
4. 读回并校验记录内容、记录 CRC、序列号和槽位；
5. 最后写入 Commit Marker；
6. 再次读回并校验；
7. 只有最终校验成功，新记录才成为有效记录；
8. Bootloader 从有效记录计算跳转地址，不直接信任任意地址字段。

因此：

- 掉电发生在 EEPROM 提交之前时，旧激活对继续有效；
- 掉电发生在提交过程中时，使用序列号、记录 CRC 和 Commit Marker 选择旧记录或新记录；
- 掉电发生在提交之后时，使用新激活对；
- 请求文件删除不参与激活事务提交，只负责避免后续重复触发。

本基线不要求 Application 再次确认升级成功。升级成功的定义是：APP 目标 CRC、GUI 目标 CRC 和 EEPROM Active Record 提交全部成功。若后续需要试运行确认，应显式增加 `TRIAL` 状态。

## 11. 恢复规则

恢复模式按以下优先级执行：

1. EEPROM 当前激活对完整有效时，继续使用当前激活对；
2. 当前激活对无效而另一对完整有效时，将另一对原子提交为激活对；
3. 请求存在但升级未提交，且旧激活对有效时，可重新执行升级；失败后不得破坏旧槽；
4. APP 已写完但 GUI 未写完时，目标对仍然无效，不允许部分切换；
5. EEPROM 双副本无效时，分别检查 APP1+GUI1 和 APP2+GUI2，只有能够确定一组完整有效发布内容时才能重建记录；
6. 两组都无效或无法确定唯一有效组时，等待 SD 卡文件系统提供合法请求和有效升级包；
7. SD 卡移除、挂载失败、请求缺失、文件缺失或包无效时保持在恢复模式，不跳转未知 APP；
8. Recovery 对升级介质可以周期性重试，但每次操作必须有界并持续喂狗；
9. Active Record 已提交但请求仍存在，且包身份与 Active Record 相同时，只清除陈旧请求，不重复擦写。

## 12. 持久化状态归属

持久化状态必须按以下边界划分：

| 状态或数据 | 存储位置 | 说明 |
|---|---|---|
| 当前激活 APP/GUI 对 | EEPROM Active Record A/B | 唯一提交依据 |
| 已提交版本、长度、CRC | EEPROM Active Record A/B | 用于启动和恢复 |
| `package_id_hash128`、`manifest_sha256` | EEPROM Active Record A/B | 用于关联发布包和避免重复安装 |
| 是否请求升级 | `/boot_update_request.json` | 文件存在且内容合法时有效 |
| 请求的 `package_id`、`manifest_sha256` | `/boot_update_request.json` | 可信授权并绑定一个确定 Manifest |
| 当前升级内部阶段、偏移、错误 | Update Service RAM | 不持久化每个 Flash 块进度 |

EEPROM Active Record 的持久化 `state` 首版只允许：

```text
EMPTY（无有效记录时的解析结果，不一定实际写入）
ACTIVE_VALID
```

以下状态不得写入 Active Record：

```text
UPDATE_REQUESTED
UPDATE_IN_PROGRESS
UPDATE_SUCCESS
```

它们分别由请求文件、Update Service RAM 状态和 Active Record 提交结果表达。

## 13. 错误决策

| 错误类别 | 新 Active Record 未提交 | 新 Active Record 已提交 |
|---|---|---|
| SD 卡不存在、移除或挂载失败 | 有旧激活对时可启动旧 APP；无有效槽时进入 Recovery | 新激活对已生效，不依赖 SD 卡 |
| 请求文件缺失 | 不进入常规升级；无有效槽时保持 Recovery | 正常启动新激活对 |
| 请求文件格式错误 | 拒绝请求，旧激活对不变 | 正常启动新激活对；可记录诊断 |
| 请求 `manifest_sha256` 或 `package_id` 与 Manifest 不匹配 | 拒绝升级，旧激活对不变 | 不适用，提交前必须完成检查 |
| 文件缺失、短读或格式错误 | 拒绝升级，旧激活对不变 | 不适用 |
| Manifest SHA、组件 SHA、兼容性或版本失败 | 拒绝升级，旧激活对不变 | 不适用 |
| 目标 APP 写入或 CRC 失败 | 整体失败，旧激活对不变 | 不适用 |
| 目标 GUI 写入或 CRC 失败 | 整体失败，旧激活对不变 | 不适用 |
| EEPROM 写入或读回失败 | 使用上一条有效记录，旧激活对不变 | 使用 CRC 和 Commit Marker 有效且序列号最新的记录 |
| 请求文件删除失败 | 不适用 | 保持新激活对，报告告警；下次启动识别并清理陈旧请求 |
| 当前激活对 CRC 失败 | 检查并切换到另一完整有效对 | 检查并切换到另一完整有效对 |
| Active Record 单副本损坏 | 使用有效副本并修复 | 使用有效副本并修复 |
| Active Record 双副本损坏 | 完整验证两组槽位并重建记录 | 完整验证两组槽位并重建记录 |
| 内部不可分类错误 | 不改变激活记录 | 进入 `FAULT` 或恢复 |

底层 HAL、FatFs、SDIO/SDMMC、I2C 和 QSPI 原生错误只能用于日志。Application 只能依据稳定的 Service 错误类别进行决策。

## 14. 启动 Application 规则

进入 `LAUNCH` 后必须：

1. 读取 EEPROM 最新有效 Active Record 并确定激活 APP/GUI 对；
2. 重新校验激活 APP 和 GUI 的槽位边界及 CRC；
3. 根据激活 APP 槽从固定表选择 `0x90000000` 或 `0x90100000`；
4. 将 W25Q256 配置为 4 字节地址模式并进入 QSPI Memory-Mapped 读模式；
5. 再次检查 Memory-Mapped Application 向量表；
6. 停止 Bootloader 拥有的周期操作；
7. 卸载或停止仍由 Bootloader 占用的文件系统/SD 操作；
8. 关闭或复位 Bootloader 使用且不能移交的外设，但保持 QSPI Memory-Mapped 执行环境；
9. 禁止中断并清除待处理中断；
10. 按约定配置 Cache、MPU、SysTick 和时钟所有权；
11. 设置 `SCB->VTOR` 为激活 APP 槽基址；
12. 设置 MSP 并跳转 Reset Handler；
13. 跳转函数若返回，立即进入 `FAULT`。

Bootloader 与 Application 必须共享明确的启动交接契约，不能依赖 Application 猜测 Bootloader 留下的外设状态。

QSPI XIP 必须满足以下规则：

- `APP1` 重定位后的向量表和运行基址固定为 `0x90000000`；
- `APP2` 重定位后的向量表和运行基址固定为 `0x90100000`；
- QSPI Memory-Mapped 窗口覆盖整个 32 MiB W25Q256；
- APP 区域在 MPU 中必须允许执行，GUI 和保留区必须禁止执行；
- APP 执行期间不得退出 Memory-Mapped 模式；
- 任何修改 QSPI 控制器或 W25Q256 模式的代码必须位于 MCU 内部 Flash/RAM；
- 间接擦写目标槽前必须退出 Memory-Mapped 模式；写入完成后必须清理或失效相关 Cache，再重新进入 Memory-Mapped 模式；
- APP 和 GUI CRC 按 Manifest 中的实际长度计算，不包含槽位剩余的 `0xFF` 填充区；
- EEPROM 保存激活槽号，不保存任意跳转地址；Bootloader 只允许从固定槽位表计算跳转地址。

同一个普通固定链接的 APP 二进制不能安全地同时运行于 APP1 和 APP2，因此继续采用安装时重定位方案：

1. APPX 使用槽位无关的规范基址生成；
2. 构建工具从最终 ELF 提取允许的重定位项，生成排序后的精简重定位表；
3. 升级包只携带一份 APPX 和一份重定位表；
4. Bootloader 根据非激活目标槽选择 `0x90000000` 或 `0x90100000`；
5. Bootloader 在分块写入 APPX 时应用重定位；
6. 向量表中的处理函数地址和其他内部绝对地址由同一重定位机制修正；
7. 重定位后的 APP 槽重新计算 CRC，CRC 通过后才允许继续 GUI 更新。

重定位表必须被 Manifest 中的 APP 文件 SHA 覆盖；正式 Application 在创建可信请求前负责验证 Manifest 签名。Bootloader 不解析通用 ELF，只解析固定版本的精简重定位表。

## 15. 已冻结参数与仍需确认项

| 参数 | 当前结论 | 状态 |
|---|---|---|
| Bootloader 分区大小 | 建议 256 KiB | 待链接脚本最终确认 |
| W25Q256 容量 | 32 MiB | 已确认 |
| APP1/APP2 大小 | 各 1 MiB | 已确认 |
| GUI1/GUI2 大小 | 各 8 MiB | 已确认 |
| QSPI 槽位地址 | `0x000000/0x100000/0x200000/0xA00000` | 已确认 |
| APP 执行方式 | QSPI Memory-Mapped/XIP | 已确认 |
| APP1/APP2 跳转地址 | `0x90000000/0x90100000` | 已确认 |
| 升级包 APP 数量 | 一份最新 APPX | 已确认 |
| APP 双地址运行方式 | 安装时重定位 | 已确认 |
| QSPI `FlashSize` | 改为 24 并真机确认 | 待验证 |
| 当前升级介质 | SD 卡 + SDIO/SDMMC + FatFs | 已确认 |
| 未来升级介质 | eMMC 文件系统 Adapter | 接口替换目标 |
| 升级请求路径 | `/boot_update_request.json` | 已冻结 |
| 升级包目录 | `/firmware` | 已冻结 |
| 发布文件名 | `manifest.json`、`hmi.app.bin`、`hmi.gui.bin` | 已冻结 |
| 请求文件 Schema | `format_version/requested/package_id/manifest_sha256` V2 | 已冻结 |
| Hash 算法 | SHA-256 | 已冻结 |
| Manifest 签名算法 | ECDSA P-256/SHA-256，由 Application/发布工具验证 | 已冻结 |
| CRC 算法参数 | CRC-32 参数和 STM32 输入字节序 | 仍需冻结 |
| Application 验签公钥介质 | 量产烧录位置、Key ID 生命周期和轮换流程 | 仍需冻结 |
| 防回滚 | 默认启用 | 版本编码细节仍需冻结 |
| Recovery 等待 | 无有效镜像时无限重试并喂狗 | 已冻结 |
| 正常启动介质等待 | 仅允许有界探测 | 已冻结 |

## 16. 不在当前基线中的功能

以下能力暂不纳入第一阶段：

- 网络升级；
- UART 升级协议；
- 多产品组件包；
- 外部设备固件级联升级；
- 工厂降级或安全解锁流程；
- 加密固件包；
- Bootloader 自更新；
- RTOS 调度；
- 文件系统中保存逐块升级进度；
- Application 试运行确认和自动回滚 `TRIAL` 状态。

新增以上能力时必须修订本文，并重新评估分区、安全、文件系统一致性和掉电恢复不变量。
