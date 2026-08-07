# STM32H7 Firmware 文档集

> 基线日期：2026-08-07  
> 状态：当前设计真相源入口  
> 目标：为人工开发、Codex 修改和后续评审提供唯一、无冲突的文档导航。

---

## 1. 当前方案一句话摘要

当前 Bootloader 采用：

> **文件系统保存升级发布包，W25Q256 只保存固定地址 Runtime；APP 与 GUI 均为无头 RAW Binary，Bootloader 不做 APPX、不做重定位、不做双 XIP 槽切换。**

正式产品以 eMMC 文件系统作为发布包存储；当前开发阶段允许使用 SD + SDMMC + FatFs 模拟相同 Package Source 行为。

---

## 2. 当前固定运行模型

### 2.1 QSPI Runtime

| 组件 | W25Q256 偏移 | CPU Memory-Mapped 地址 | 最大大小 |
|---|---:|---:|---:|
| APP Runtime | `0x000000` | `0x90000000` | 1 MiB |
| GUI Runtime | `0x200000` | `0x90200000` | 8 MiB |

Application 只针对这一组地址链接：

```text
APP link/runtime base = 0x90000000
GUI resource base     = 0x90200000
```

不存在 APP2/GUI2 运行地址。

### 2.2 文件系统发布包

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

`hmi.app.bin` 与 `hmi.gui.bin` 均为原始 payload：

```text
hmi.app.bin offset 0 -> W25Q256 offset 0x000000 -> CPU 0x90000000
hmi.gui.bin offset 0 -> W25Q256 offset 0x200000 -> CPU 0x90200000
```

---

## 3. 当前安全边界

生产链路：

```text
Application / trusted provisioning
    -> 完成 Manifest 发布认证/验签
    -> 完成文件写入、关闭、同步和完整性检查
    -> 最后创建 boot_update_request.json
       并绑定 manifest_sha256
    -> Reset
    -> Bootloader
       -> 信任该 request handoff
       -> 校验 request 与 Manifest 的绑定
       -> 校验 APP/GUI SHA-256
       -> 安装固定 Runtime
```

Bootloader 当前明确不承担：

- Manifest ECDSA 验签；
- 公钥、Key ID、密钥轮换；
- APPX 解析；
- ELF 解析；
- APP/GUI relocation。

重要说明：

> `boot_update_request.json` 的“可信”是系统级 handoff 合同，不是普通 JSON/FAT 文件天然具备的密码学属性。生产系统必须保证 request 的创建与后续存储不允许未授权修改；若威胁模型允许离线篡改 eMMC，则必须增加受保护存储、MAC/签名或其他认证机制。

开发阶段手工 SD 卡创建 request 属于 **development trust override**，不得描述为量产安全认证。

---

## 4. 掉电恢复模型

W25Q256 不再保留第二套 Runtime，因此升级过程中允许当前 Runtime 被覆盖。

掉电安全依赖：

1. 发布包在文件系统中保持完整；
2. request 在 Runtime 安装成功并提交前不得清除；
3. APP/GUI 源文件必须在第一次目标擦除前完成 SHA-256 校验；
4. APP/GUI 写入后必须回读并再次校验；
5. Active Record 只在 APP+GUI 都完整验证后提交；
6. request 只在 Active Record 提交后清除。

如果升级中途掉电：

```text
request 仍存在
    -> 下次 Bootloader 重新处理同一发布包
    -> 当前 Runtime 若不完整则不得 Launch
    -> 重新安装直到成功
```

当前基线 **不定义“新版本启动失败后自动回滚到上一个版本”**。若未来需要 Trial/Confirm/Rollback，再单独扩展业务规则和详细设计。

---

## 5. 已废弃设计

以下设计从当前基线删除，Codex 不得重新引入：

```text
APP1 / APP2 交替 XIP
GUI1 / GUI2 成对切换
active_pair / inactive_pair
slot_policy 选择运行槽
APPX Header
hmi.app.reloc.bin
raw-xip-reloc-v2
ABS32_ADD_XIP_BASE
Bootloader 安装时 relocation
target_crc32.app1 / target_crc32.app2
从 QSPI pair 1 / pair 2 扫描 Recovery 候选
Manifest 指定任意目标写入地址
```

---

## 6. 正式文档及职责

### 01 `Architecture/stm32h7_firmware_software_architecture.md`

回答：

> STM32H7 固件工程整体如何分层、依赖和组织？

只定义软件结构，不定义具体 Bootloader 升级业务。

### 02 `Architecture/stm32h7_application_services_architecture.md`

回答：

> 一段逻辑应该属于 Application、Service、Interface 还是 Adapter？

定义对象模型、状态机粒度、长流程执行、错误模型和依赖注入。

### 03 `Bootloader/stm32h7_bootloader_business_rules.md`

回答：

> Bootloader 在启动、升级、失败、掉电和恢复场景中必须怎么做？

它是 Bootloader 业务行为的唯一真相源。

### 04 `Bootloader/stm32h7_firmware_release_package_contract.md`

回答：

> Application/发布工具和 Bootloader 通过文件系统交换什么文件、格式和可信 handoff？

它是 request、Manifest、RAW Binary 和创建/清理顺序的唯一真相源。

### 05 `Bootloader/stm32h7_bootloader_detailed_design.md`

回答：

> 如何用具体模块、状态机、EEPROM Record、Interface、Adapter 和测试实现上述规则？

---

## 7. 阅读顺序

Codex 或开发人员修改 Bootloader 前必须按顺序阅读：

```text
README
  -> Firmware Software Architecture
  -> Application / Services Architecture
  -> Bootloader Business Rules
  -> Firmware Release Package Contract
  -> Bootloader Detailed Design
```

发生冲突时，优先级为：

```text
Business Rules
    > Release Package Contract
    > Detailed Design
    > 通用 Architecture 示例
```

Architecture 文档不得覆盖具体 Bootloader 业务结论。

---

## 8. 当前范围外

当前不冻结：

- Trial Boot / Confirm / Revert；
- 自动回滚到上一版本；
- `/rollback/` 目录；
- Bootloader 自更新；
- 加密发布包；
- 网络 OTA；
- 外部 MCU 联动升级；
- eMMC 文件系统具体实现类型；
- QSPI 剩余区域最终用途。

这些能力不得由 Codex自行假设并加入当前基线。
