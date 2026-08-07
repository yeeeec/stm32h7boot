# STM32H7 Bootloader 业务规则基线

> 状态：Business Baseline v1  
> MCU：STM32H743  
> 外部 Runtime Flash：W25Q256 32 MiB  
> 正式发布源：eMMC 文件系统  
> 当前开发发布源：SD + SDMMC + FatFs（行为模拟正式 Package Source）

---

## 1. 核心业务结论

当前 Bootloader 采用固定 Runtime 模型：

```text
Filesystem
    = Release Package Source
    = 安装中断后的重新安装来源

W25Q256
    = Fixed Runtime Store

EEPROM
    = 当前已确认 Runtime 的原子元数据记录
```

明确取消：

```text
APP1/APP2
GUI1/GUI2
Active Pair
Inactive Pair
Direct-XIP A/B
APP relocation
APPX
QSPI pair recovery
```

---

## 2. 固定 Runtime 布局

| 区域 | W25Q256 偏移 | CPU 地址 | 最大大小 |
|---|---:|---:|---:|
| APP Runtime | `0x000000` | `0x90000000` | 1 MiB |
| Reserved | `0x100000..0x1FFFFF` | `0x90100000..0x901FFFFF` | 1 MiB |
| GUI Runtime | `0x200000` | `0x90200000` | 8 MiB |
| Remaining | `0xA00000..0x1FFFFFF` | `0x90A00000..0x91FFFFFF` | 22 MiB |

剩余空间不得被当前 Bootloader 隐式作为备份槽。

Application 永远从：

```text
0x90000000
```

启动，并永远按：

```text
GUI base = 0x90200000
```

访问当前 GUI 资源。

---

## 3. 发布包

固定：

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

业务上 APP + GUI 是一个不可拆分 Release：

- 共用 package identity；
- 共用 release version；
- 必须同时通过验证；
- 不允许提交“新 APP + 旧 GUI”；
- 不允许提交“旧 APP + 新 GUI”。

具体文件字段见 Release Package Contract。

---

## 4. Trusted Update Request

`boot_update_request.json` 同时承担：

1. 触发升级；
2. 向 Bootloader 表示上游已完成发布认证；
3. 将授权绑定到本次精确 Manifest。

生产链路：

```text
Trusted Application / Provisioning
 -> 验证 Manifest 发布签名/认证
 -> 写完并校验 manifest/app/gui
 -> 最后创建 request
 -> request.manifest_sha256 绑定实际 Manifest
 -> Reset
```

Bootloader：

- 必须读取并严格验证 request；
- 必须校验 `manifest_sha256`；
- 必须校验 request `package_id` 与 Manifest 一致；
- 不再次执行 Manifest ECDSA 验签；
- 不持有发布公钥。

安全前提：

> request store 必须属于受信 handoff。若未授权主体可以同时修改 request 与发布包，则本基线不能提供发布来源认证。

开发阶段人工 SD request 属于 development trust override。

---

## 5. 顶层模式

建议：

| 状态 | 语义 |
|---|---|
| `STARTUP` | 初始化 Boot Control、Flash、Package Source |
| `UPDATE_CHECK` | 有界探测 request |
| `UPDATE` | 验证发布包并安装固定 Runtime |
| `VALIDATE_RUNTIME` | 验证当前固定 Runtime |
| `RECOVERY` | 尝试利用仍存在的受信发布包重新安装 |
| `RESET` | 成功安装提交后的系统复位 |
| `LAUNCH` | 跳转固定 `0x90000000` |
| `FAULT` | 无法获得已验证 Runtime |

正常启动不得无界等待可移除 SD；正式 eMMC 仍需所有 I/O 有超时和错误出口。

---

## 6. 决策优先级

### 6.1 有有效 request

```text
Load request
 -> 验证 request
 -> 验证 Manifest binding
 -> Prepare/Policy
 -> 验证完整 APP/GUI Source
 -> Install
 -> Commit Active Record
 -> Clear request
 -> Reset
```

在 request 已被接受后，Bootloader不得直接忽略它并启动未知的混合 Runtime。

### 6.2 无 request

```text
Load latest valid Active Record
 -> Validate Runtime
 -> Launch
```

如果 Runtime 与 Active Record 不一致：

```text
-> RECOVERY
-> 若没有可用的已授权恢复源
-> FAULT
```

当前基线不自动选择历史版本。

---

## 7. Prepare 与擦除前不变量

第一次擦除 W25Q256 Runtime 前，必须全部完成：

1. request Schema 有效；
2. request `requested == true`；
3. request `manifest_sha256` 与实际 Manifest SHA-256 一致；
4. request `package_id` 与 Manifest 一致；
5. Manifest Schema、产品、硬件和数值范围有效；
6. `minimum_bootloader_version` 被接受；
7. 发布版本策略被 Application 接受；
8. APP 文件大小有效；
9. GUI 文件大小有效；
10. APP 完整 SHA-256 与 Manifest 一致；
11. GUI 完整 SHA-256 与 Manifest 一致。

也就是说：

> **所有源文件完整性必须在第一次破坏当前 Runtime 之前完成。**

---

## 8. 安装事务

固定顺序：

```text
MOUNT SOURCE
-> LOAD + VALIDATE TRUSTED REQUEST
-> PREPARE MANIFEST
-> POLICY DECISION
-> VERIFY APP SOURCE SHA256
-> VERIFY GUI SOURCE SHA256

-> ERASE APP RUNTIME
-> PROGRAM APP RUNTIME
-> READBACK SHA256 APP

-> ERASE GUI RUNTIME
-> PROGRAM GUI RUNTIME
-> READBACK SHA256 GUI

-> CREATE ACTIVE RECORD CANDIDATE
-> ATOMIC COMMIT ACTIVE RECORD

-> CLEAR REQUEST
-> UNMOUNT
-> SYSTEM RESET
```

APP/GUI 写入过程不做任何地址重定位。

---

## 9. 失败规则

### 9.1 第一次目标擦除前失败

例如：

- request 无效；
- Manifest 无效；
- Source SHA 不匹配；
- 版本拒绝。

此时不得修改 W25Q256 Runtime。

Bootloader可以验证并启动现有 Active Runtime。

### 9.2 第一次目标擦除后失败

例如：

- APP erase/program/readback 失败；
- GUI erase/program/readback 失败；
- 介质在安装中断开。

此时 Runtime 可能已经不可启动。

规则：

1. 不提交新的 Active Record；
2. 不清除 request；
3. 不允许因为旧 Active Record 仍存在就盲目 Launch；
4. 必须验证 Runtime；
5. Runtime 与旧 Active Record 不一致时进入 RECOVERY；
6. 若受信发布源可用，重新安装；
7. 无法恢复则进入 FAULT。

---

## 10. 掉电规则

### 10.1 request 创建前掉电

旧 Runtime 不受影响。

### 10.2 request 创建后、擦除前掉电

下次重新 Prepare。

### 10.3 APP/GUI 擦写中掉电

request 仍存在。

下次：

```text
重新验证 request + package
-> 重新安装固定 Runtime
```

不得尝试从“另一 QSPI 槽”恢复，因为不存在另一 Runtime 槽。

### 10.4 APP/GUI 已验证、Active Record 提交前掉电

request 仍存在，重新处理同一 Release；允许重新安装或验证后重新提交。

### 10.5 Active Record 提交中掉电

EEPROM A/B 原子记录必须保证只能选到旧有效记录或完整新记录。

若旧记录被选中但 Runtime 已是新版本：

- request 仍存在；
- Bootloader重新处理该发布包；
- 最终重新提交。

### 10.6 Active Record 提交后、request 清除前掉电

下次 request 与 Active Record 的：

```text
package_id_hash128
manifest_sha256
```

一致。

此时先验证 Runtime：

- Runtime 正确 -> 识别为 stale request，只清除 request；
- Runtime 不正确 -> 从同一发布包重新安装。

---

## 11. Stale Request

相同发布包判定：

```text
active.package_id_hash128 == hash128(manifest.package_id)
AND
active.manifest_sha256 == SHA256(actual manifest bytes)
```

不能仅使用版本号。

Stale request 不得导致反复擦写一个已经正确安装的 Runtime。

---

## 12. Active Record 业务语义

EEPROM A/B 双副本机制继续保留。

但 Active Record 现在表示：

> **固定 QSPI Runtime 当前已确认安装的 Release Metadata。**

它不再表示：

```text
active_pair
active_slot
jump_address
```

提交点仍然是：

> APP+GUI 已完整写入并回读验证成功后的唯一持久化“安装成功”确认。

request 清除不是提交点。

---

## 13. Runtime Validation

进入 `LAUNCH` 前至少必须：

1. 选出有效 Active Record；
2. APP 大小在固定分区范围内；
3. GUI 大小在固定分区范围内；
4. APP Runtime 与 Active Record 期望 SHA-256 一致；
5. GUI Runtime 与 Active Record 期望 SHA-256 一致；
6. Initial MSP 位于允许 SRAM；
7. Reset Handler Thumb bit 为 1；
8. Reset Handler 地址位于固定 APP Runtime 有效范围。

如果后续因启动时间优化要减少每次 full SHA，必须另立性能/安全规则，不允许 Codex自行省略。

---

## 14. Launch

固定：

```text
VTOR / vector base = 0x90000000
```

Launch Service：

1. 配置 W25Q256 4-byte addressing；
2. 进入稳定 QSPI Memory-Mapped；
3. 配置 MPU/Cache；
4. 停止 SysTick；
5. 禁用并清理中断；
6. 校验 MSP/Reset Handler；
7. 设置 VTOR；
8. 设置 MSP；
9. DSB/ISB；
10. 跳转；
11. 返回视为错误。

不存在第二个 XIP jump base。

---

## 15. Recovery 当前基线

当前 `RECOVERY` 只保证：

> 安装过程中断后，如果同一受信发布包仍在文件系统中，则可以重新安装固定 Runtime。

当前不定义：

- `/rollback/`；
- 上一个已确认版本；
- Trial Boot；
- Boot attempt counter；
- 自动 Revert。

当前 Runtime 无效且没有可用受信发布源时进入 `FAULT`。

---

## 16. EEPROM

EEPROM 继续只使用 Active Record A/B。

当前不要求保存：

- Request 文件；
- 文件路径；
- 任意跳转地址；
- slot/pair；
- 逐块进度。

Record 的结构与原子写算法见 Detailed Design。

---

## 17. 正式/开发介质

### 正式

```text
eMMC filesystem
```

### 开发

```text
SD + SDMMC + FatFs
```

两者必须实现同一稳定 Interface。

业务层不得写：

```c
if (using_sd) { ... }
else if (using_emmc) { ... }
```

---

## 18. 当前范围外

- Trial/Confirm/Revert；
- 自动历史版本回滚；
- Bootloader 自更新；
- 网络 OTA；
- 加密 Payload；
- 运行时 APP relocation；
- Direct-XIP A/B；
- 双链接 APP A/B；
- QSPI 第二 Runtime 备份；
- Manifest 提供写入目标地址。

---

## 19. 业务验收

- [ ] Bootloader 永远只跳 `0x90000000`。
- [ ] GUI 永远只按 `0x90200000` Runtime 设计。
- [ ] 发布目录不存在 relocation 文件。
- [ ] `hmi.app.bin` 写入时一个字节不做地址变换。
- [ ] request 绑定实际 Manifest SHA-256。
- [ ] APP/GUI Source SHA 在第一次 Runtime 擦除前完成。
- [ ] APP+GUI readback SHA 均成功后才 Commit Active Record。
- [ ] Commit 前不得 clear request。
- [ ] Commit 后 clear 失败不回退已安装 Runtime。
- [ ] 掉电后没有任何“切换到另一 QSPI pair”的逻辑。
- [ ] Runtime 无效且无受信 Recovery Source 时进入 FAULT。
