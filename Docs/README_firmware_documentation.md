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
UPDATE
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

当前基线 **不定义“新版本启动失败后自动回滚到上一个版本”**。

---

## 6. 正式文档及职责

### 01 `Architecture/stm32h7_firmware_software_architecture.md`

回答：

> STM32H7 固件工程整体如何分层、依赖和组织？

只定义软件结构，不定义具体 Bootloader 升级业务。
