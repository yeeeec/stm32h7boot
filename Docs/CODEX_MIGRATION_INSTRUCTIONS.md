# Codex 迁移执行说明

> 类型：一次性实施任务单  
> 注意：本文件不是架构真相源。修改完成后，正式规则以 `README + Architecture + Bootloader` 6 份文档为准。

---

## 1. 目标

把现有工程从：

```text
QSPI 双 APP/GUI Pair
+ Active Pair
+ Relocation
+ request 仅存在性触发
```

迁移为：

```text
Filesystem Release Source
+ Trusted Request binding Manifest
+ Fixed QSPI Runtime
+ RAW APP/GUI
+ EEPROM Active Record V2
```

固定地址：

```text
APP Runtime = W25Q256 offset 0x000000 / CPU 0x90000000 / 1 MiB
GUI Runtime = W25Q256 offset 0x200000 / CPU 0x90200000 / 8 MiB
```

---

## 2. Codex 开始前必须做

不要第一步就大规模修改。

先执行：

```bash
git status
git branch --show-current
```

确保当前工作树状态明确。

然后阅读：

```text
Docs/README_firmware_documentation.md
Docs/Architecture/stm32h7_firmware_software_architecture.md
Docs/Architecture/stm32h7_application_services_architecture.md
Docs/Bootloader/stm32h7_bootloader_business_rules.md
Docs/Bootloader/stm32h7_firmware_release_package_contract.md
Docs/Bootloader/stm32h7_bootloader_detailed_design.md
```

把它们视为新基线。

---

## 3. 第一步：只做旧设计 Inventory

全仓库搜索：

```bash
rg -n \
'APP1|APP2|GUI1|GUI2|active_pair|inactive_pair|target_pair|slot_policy|APPX|appx|reloc|relocation|hmi\.app\.reloc\.bin|raw-xip-reloc|ABS32_ADD_XIP_BASE|target_crc32|pair 1|pair 2'
```

再搜索 Request 旧语义：

```bash
rg -n \
'boot_update_request|request.*exists|不读取.*request|不解析.*request|request.*不认证|trusted_request'
```

再搜索构建工具：

```bash
rg -n \
'build_appx|emit-relocs|readelf.*-r|objcopy|RESOURCE_FLASH|0x90000000|0x90200000'
```

输出一个 migration inventory，按以下分类：

```text
A. 必删旧业务类型
B. 必改接口
C. 必改 Service
D. 必改 Application
E. 必改 Adapter/Composition
F. 必改 Manifest/Request Parser
G. 必改 EEPROM Record
H. 必改 Linker/Post-build
I. 必改 Tests
```

完成 inventory 前不要删除代码。

---

## 4. 第二步：先冻结基础类型，不动流程

优先修改：

### 4.1 Runtime Layout

删除/废弃：

```text
slot_policy
APP_SLOT_1
APP_SLOT_2
GUI_SLOT_1
GUI_SLOT_2
pair mapping
```

新增：

```c
boot_runtime_layout_t
```

固定：

```text
APP offset = 0
APP XIP    = 0x90000000
APP max    = 1 MiB

GUI offset = 0x200000
GUI mmap   = 0x90200000
GUI max    = 8 MiB
```

### 4.2 Active Record V2

保留 A/B 原子写算法。

删除：

```text
active_pair
```

新增：

```text
app_sha256
gui_sha256
```

升级 `format_version = 2`。

不要把 V1 静默 reinterpret 为 V2。

先完成 encode/decode/CRC/A-B tests，再继续上层修改。

---

## 5. 第三步：修改 Release Model

删除：

```text
hmi.app.reloc.bin
relocation metadata
target_crc32.app1/app2
link target selection
APPX parser
```

Manifest model 改成：

```text
package identity
release version
target compatibility
minimum bootloader version
app: file/format/size/sha256
gui: file/format/size/sha256
```

增加 Request model：

```text
format_version
requested
package_id
manifest_sha256
```

完成 parser Host tests。

---

## 6. 第四步：拆 Request 与 Package Interface

目标：

```text
package_source_t
    只负责发布包读取

update_request_store_t
    只负责固定 request load/clear
```

不要让 Service 收到任意 request path。

开发构建绑定 SD/FatFs。

正式 eMMC Adapter 可以先保留 Stub/Interface，如果当前硬件还没有 eMMC。

---

## 7. 第五步：重构 Update Service

删除阶段：

```text
SELECT_TARGET
LOAD_RELOCATION
VALIDATE_RELOCATION
APPLY_RELOCATION
```

改成：

```text
PREPARE_MANIFEST
VERIFY_APP_SOURCE
VERIFY_GUI_SOURCE

ERASE_APP_RUNTIME
PROGRAM_APP_RUNTIME
VERIFY_APP_RUNTIME

ERASE_GUI_RUNTIME
PROGRAM_GUI_RUNTIME
VERIFY_GUI_RUNTIME

BUILD_RECORD_CANDIDATE
```

关键顺序：

> APP 和 GUI 两个 Source SHA 都完成后，才允许进入第一次 APP erase。

不要边写 APP 边到最后才发现 GUI Source 已损坏。

---

## 8. 第六步：修改 Application

Application 顶层：

```text
STARTUP
UPDATE_CHECK
UPDATE
VALIDATE_RUNTIME
RECOVERY
RESET
LAUNCH
FAULT
```

更新逻辑：

```text
mount
-> request_store.load
-> Update Prepare
-> stale/version policy
-> Update Install
-> BootControl Commit
-> request_store.clear
-> unmount
-> reset
```

删除：

```text
select inactive pair
commit active pair
launch pair-specific address
```

Request clear 失败不回退。

---

## 9. 第七步：删除 Pair Recovery

删除：

```text
recovery_candidate_load_fn(pair)
scan pair1/pair2
choose recovery pair
```

当前 V1：

```text
VALIDATE_RUNTIME failed
-> if trusted request/package available
   -> reuse Update Install
-> else FAULT
```

可以暂时保留 `RECOVERY` Application 状态，但不要保留双槽候选算法。

---

## 10. 第八步：Launch 固定化

Launch Service 不再接受 pair 地址。

固定：

```text
vector base = 0x90000000
```

删除任何：

```text
0x90100000
0x90A00000 as APP jump
0x90C00000
```

注意 `0x90A00000` 可能以后是普通 Reserved 地址，不要仅按数值全仓库粗暴删除，必须逐处确认语义。

---

## 11. 第九步：Linker/Post-build

Application LD：

```ld
FLASH (rx) :
    ORIGIN = 0x90000000, LENGTH = 1M

RESOURCE_FLASH (rx) :
    ORIGIN = 0x90200000, LENGTH = 8M
```

确保 TouchGFX Sections 仍：

```text
> RESOURCE_FLASH
```

Post-build：

```text
application.elf
 -> hmi.app.bin
 -> hmi.gui.bin
 -> manifest.json
```

删除：

```text
build_appx.py
relocation generator
--emit-relocs 仅为 relocation 所需的依赖
```

如果 `--emit-relocs` 还有别的调试用途，可以保留，但不得再作为 Bootloader Release Contract 的依赖。

必须验证 `hmi.app.bin` 不包含 `0x90000000 -> 0x90200000` 的大地址洞和 GUI payload。

---

## 12. 第十步：测试顺序

先 Host，后 Target。

### Host

1. Active Record V2；
2. Request parser；
3. Manifest parser；
4. Request ↔ Manifest hash binding；
5. Source SHA；
6. Runtime layout bounds；
7. Update state machine；
8. stale request；
9. power-loss state decision；
10. no pair/relocation symbols。

### Target

1. fixed APP XIP；
2. fixed GUI mapping；
3. APP update；
4. GUI update；
5. APP/GUI readback SHA；
6. random power loss；
7. stale request；
8. Cache/MPU/QSPI mode transition。

---

## 13. 每个迁移阶段都必须保持可编译

推荐提交粒度：

```text
commit 1: docs + runtime layout types
commit 2: Active Record V2
commit 3: Request/Manifest model
commit 4: Interface/Adapter changes
commit 5: Update Service fixed-runtime install
commit 6: Application orchestration
commit 7: remove pair recovery/relocation
commit 8: linker/post-build
commit 9: tests/cleanup
```

不要一次提交全部迁移。

---

## 14. 最终静态检查

必须保证：

```bash
rg -n \
'APP2|GUI2|active_pair|inactive_pair|hmi\.app\.reloc\.bin|raw-xip-reloc|ABS32_ADD_XIP_BASE|relocation_entry_t|target_crc32\.app1|target_crc32\.app2'
```

在正式代码和正式文档中无业务残留。

允许历史 archive 中出现，但 archive 不得被 CMake、Codex instruction 或当前 README 引用。

---

## 15. Codex 不得自行扩展

本次迁移不要顺便实现：

```text
rollback/
trial boot
confirm/revert
boot attempt counter
network OTA
bootloader self update
encrypted payload
dual-linked APP
new QSPI secondary slot
```

发现相关需求只记录 TODO，不实现。

---

## 16. 完成定义

迁移完成必须同时满足：

- 构建只产生 `manifest.json + hmi.app.bin + hmi.gui.bin`；
- Request 是带 Manifest binding 的结构化文件；
- Bootloader只跳 `0x90000000`；
- GUI固定 `0x90200000`；
- APP/GUI 不做 relocation；
- EEPROM Record 无 `active_pair`；
- Upgrade Source 在第一次 erase 前完成完整 SHA；
- 写后完成 APP/GUI readback SHA；
- Commit 后才 clear request；
- power loss 后可依靠仍存在的受信 package 重装；
- 无 package 且 Runtime 无效时进入 FAULT；
- 全部 Host tests 和 Target smoke tests 通过。
