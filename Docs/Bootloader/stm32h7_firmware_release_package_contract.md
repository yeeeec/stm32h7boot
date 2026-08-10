# STM32H7 Firmware Release Package Contract

> 状态：Release Contract v1  
> 参与方：发布工具 / Application / Bootloader  
> 目标：冻结文件布局、Trusted Request、Manifest、RAW Binary、完整性和生命周期。

---

## 1. 文件布局

固定：

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    ├── hmi.gui.bin
    └── therapy.app.bin
```

三个 payload 都是可选的，但每个包至少包含一个被请求选择的组件。
`therapy.app.bin` 是供第三方 MCU 串口升级使用的无头 RAW 镜像；打包工具暂不负责生成它。

规则：

- 路径大小写固定；
- 不扫描目录；
- 不接受 Manifest 指定任意路径；
- 同一时刻只处理一个 candidate release；
- 不存在 `hmi.app.reloc.bin`；
- 不存在 APPX 容器。

---

## 2. 介质角色

正式产品：

```text
eMMC filesystem
```

开发阶段：

```text
SD + FatFs
```

文件语义完全相同。

手工 SD 发布是 development trust override，不代表量产发布认证。

---

## 3. `hmi.app.bin`

定义：

> 针对固定 Runtime `0x90000000` 链接并从最终 ELF 中提取出的无头 RAW APP Payload。

要求：

- 无 APPX Header；
- 无 relocation table；
- 无 trailer；
- 文件 offset 0 对应 W25Q256 APP Runtime offset `0x000000`；
- 文件最大 1 MiB；
- Bootloader不得修改 payload 内容；
- Bootloader不得按 Manifest 提供的任意地址写入。

安装：

```text
hmi.app.bin[0]
    -> W25Q256 offset 0x000000
    -> CPU 0x90000000
```

---

## 4. `hmi.gui.bin`

定义：

> 从最终 Application ELF 的 `RESOURCE_FLASH`/TouchGFX 外置资源 Sections 提取出的无头 RAW GUI Payload。

要求：

- 文件 offset 0 对应 W25Q256 GUI Runtime offset `0x200000`；
- CPU Memory-Mapped base `0x90200000`；
- 最大 8 MiB；
- Bootloader原样写入；
- 当前 Contract 假设 GUI payload 不需要自身 relocation。

安装：

```text
hmi.gui.bin[0]
    -> W25Q256 offset 0x200000
    -> CPU 0x90200000
```

构建验证必须证明 APP ELF 中 TouchGFX 资源 VMA 与固定 `0x90200000` 契约一致。

---

## 5. Build 输出合同

Application Linker Script 必须至少约束：

```ld
FLASH (rx) :
    ORIGIN = 0x90000000,
    LENGTH = 1M

RESOURCE_FLASH (rx) :
    ORIGIN = 0x90200000,
    LENGTH = 8M
```

禁止继续使用会掩盖边界问题的：

```text
FLASH LENGTH = 2 MiB
RESOURCE_FLASH LENGTH = 30 MiB
```

构建输出流程：

```text
application.elf
   │
   ├── APP Runtime sections
   │       -> hmi.app.bin
   │
   └── RESOURCE_FLASH sections
           -> hmi.gui.bin
```

禁止简单把包含两个非连续地址区间的完整 ELF 直接生成一个整体 binary 作为 `hmi.app.bin`。

Post-build 工具必须：

1. 检查 APP sections 全部落入 1 MiB APP region；
2. 检查 GUI sections 全部落入 8 MiB GUI region；
3. 分别生成两个 RAW Binary；
4. 计算两个文件大小；
5. 计算两个 SHA-256；
6. 生成 Manifest；
7. 生产 Application 完成发布认证后才允许创建 Trusted Request。

---

## 6. Manifest 最小 Schema

推荐 V1（`components` 可包含 APP、GUI、therapy 的任意非空子集）：

```json
{
  "format_version": 1,
  "package_id": "hmi-1.2.3-20260807",
  "release": {
    "major": 1,
    "minor": 2,
    "patch": 3,
    "build": 20260807
  },
  "target": {
    "product": "HMI",
    "hardware": "STM32H743-W25Q256",
    "minimum_bootloader_version": "1.0.0"
  },
  "components": {
    "app": {
      "file": "hmi.app.bin",
      "format": "raw-bin-v1",
      "size": 524288,
      "sha256": "..."
    },
    "gui": {
      "file": "hmi.gui.bin",
      "format": "raw-bin-v1",
      "size": 4194304,
      "sha256": "..."
    },
    "therapy": {
      "file": "therapy.app.bin",
      "format": "raw-bin-v1",
      "size": 262144,
      "sha256": "..."
    }
  }
}
```

Bootloader 固定识别：

```text
components.app.file == "hmi.app.bin"
components.gui.file == "hmi.gui.bin"
components.therapy.file == "therapy.app.bin"
```

therapy payload 最大 512 KiB，版本使用 Manifest 的 `release` 三元组，升级时不得低于
已持久化的 therapy 版本；不执行额外兼容性策略。

Manifest 不提供：

```text
target_address
slot
pair
relocation
link delta
jump address
```

物理 Runtime 地址由 Bootloader 固定产品配置决定。

---

## 7. Manifest 发布认证字段

生产 Manifest 可以包含 Application/发布系统使用的签名字段。

原则：

- Application/发布工具负责认证；
- Bootloader 不执行签名验证；
- Bootloader 不输出“签名有效”结论；
- Bootloader只信任成功创建的 Trusted Request handoff；
- Request 绑定完整原始 Manifest bytes 的 SHA-256。

若 Bootloader 使用严格 JSON Schema，则签名对象必须作为已知但不参与 Bootloader认证结论的字段明确冻结；不得以“未知字段自动忽略”破坏 Schema 严格性。

签名对象具体算法与 Key Lifecycle 不在本 Contract 中展开。

---

## 8. Trusted Request Schema

推荐：

```json
{
  "format_version": 1,
  "requested": true,
  "package_id": "hmi-1.2.3-20260807",
  "component_mask": 5,
  "manifest_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

约束：

- `format_version == 1`；
- `requested == true`；
- `package_id` 长度、字符集有固定上限；
- `component_mask` 必须为 `1..7` 的非零位掩码：APP=`1`、GUI=`2`、therapy=`4`；
- 省略 `component_mask` 的历史 V1 请求按 APP+GUI（掩码 `3`）解释；
- `manifest_sha256` 必须为 64 个小写 hex；
- 不允许未知字段；
- 不允许目标地址；
- 不允许指定任意文件路径；
- 不允许指定目标 slot/pair。

Bootloader处理：

```text
SHA256(actual manifest bytes)
    == request.manifest_sha256

manifest.package_id
    == request.package_id
```

两项都必须成立。

---

## 9. Request 的信任定义

`boot_update_request.json` 是：

> **受信发布 handoff token 的文件系统表示。**

它的可信性不来自 JSON 格式本身。

生产安全前提：

1. 只有通过发布认证的 Application/Provisioning 流程可以创建有效 request；
2. 创建后直到 Bootloader消费期间，request 与 package 不允许未授权修改；
3. 如果存储介质属于攻击者可离线写入的威胁模型，应增加认证 request 或受保护存储。

当前基线故意不把签名验证逻辑复制进 Bootloader。

---

## 10. Application 创建顺序

必须：

```text
1. 写 manifest.json
2. 写 hmi.app.bin
3. 写 hmi.gui.bin
4. close 所有文件
5. filesystem sync
6. 重新读取并验证文件 SHA-256
7. 完成 Manifest 发布认证/验签
8. 创建 boot_update_request.json
9. filesystem sync
10. system reset
```

原则：

> request 永远最后创建。

---

## 11. Bootloader 消费顺序

```text
mount
-> load trusted request
-> read raw manifest bytes
-> SHA256(manifest)
-> compare request.manifest_sha256
-> strict parse Manifest
-> compare package_id
-> policy checks
-> policy checks for each selected component
-> verify complete SHA256 for each selected source
-> install selected APP/GUI components
-> readback verify selected APP/GUI components
-> hash-check and serial-program selected therapy image
-> commit Active Record
-> clear request
-> unmount
-> reset
```

---

## 12. Stale Request

如果 Active Record 已记录：

```text
package_id_hash128
manifest_sha256
```

且当前 request/Manifest 与其一致，则：

1. 先验证固定 Runtime；
2. Runtime 与 Active Record 一致 -> 不重新安装，只 clear request；
3. Runtime 不一致 -> 允许使用同一受信发布包重新安装。

这解决：

```text
Active Record commit 成功
+
request clear 前掉电
```

导致的重复擦写问题。

---

## 13. Source Integrity

APP/GUI/therapy 必须使用 SHA-256。

第一次 Runtime 擦除前必须验证完整文件，而不是边读边写后才发现末尾损坏。

Bootloader可使用 4 KiB buffer 分块计算 SHA，不要求把文件整体放入 RAM。

---

## 14. Target Verification

因为安装不修改 payload：

```text
source bytes == target bytes
```

所以写入后按实际组件 size 从 W25Q256 回读并计算 SHA-256：

```text
SHA256(QSPI APP Runtime[0..app.size))
    == manifest.app.sha256

SHA256(QSPI GUI Runtime[0..gui.size))
    == manifest.gui.sha256
```

不需要：

```text
source_crc32
target_crc32.app1
target_crc32.app2
relocation_crc32
```

EEPROM Record 自身允许继续使用 record CRC 来检测元数据写坏；它与固件内容认证无关。

---

## 15. Manifest Parser 规则

必须拒绝：

- 重复 Key；
- 未知字段；
- 缺失必需字段；
- 类型错误；
- 负 size；
- 数值溢出；
- 超过 APP/GUI 分区上限；
- 错误固定文件名；
- 非法 SHA hex；
- 不支持的 `format_version`；
- 不支持的 `raw-bin` format；
- 错误 product/hardware。

Manifest 最大长度应有固定上限，例如 16 KiB；具体 token workspace 见 Detailed Design。

---

## 16. 文件清理

只能在：

```text
all selected targets verified
AND Active Record atomic commit succeeded
```

之后 clear request。

request clear 失败：

- 不回退 Runtime；
- 不改写 Active Record；
- 仍可 reset；
- 下次通过 stale request 规则处理。

当前 Contract 不要求删除 `/firmware/` 下组件；正式产品可保留 candidate 作为安装后诊断来源，但不能把它自动解释为上一版本 rollback。

---

## 17. 验收

- [ ] 目录只有 Manifest + APP + GUI 三个发布文件。
- [ ] APP/GUI 都是无头 RAW Binary。
- [ ] 无 APPX magic。
- [ ] 无 relocation 文件。
- [ ] Manifest 不含目标 slot/address。
- [ ] request 含 `manifest_sha256`。
- [ ] request `package_id` 与 Manifest一致。
- [ ] Source SHA 在擦除前完整验证。
- [ ] Target SHA 按写入后实际内容回读验证。
- [ ] request 最后创建。
- [ ] request 最后清除。
