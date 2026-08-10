# STM32H7 升级文件制作指南

> 适用工程：`stm32h7app` + `stm32h7boot`  
> 适用介质：开发阶段 SD 卡，正式产品文件系统  
> 更新日期：2026-08-10

本文说明如何从 Application ELF 生成 Bootloader 可识别的 APP/GUI 升级文件，完成完整性校验，并写入 SD 卡。升级包协议的完整定义参见 [STM32H7 Firmware Release Package Contract](stm32h7_firmware_release_package_contract.md)。

## 1. 最终文件结构

Bootloader 只接受以下固定目录和文件名：

```text
SD 卡根目录/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

规则：

- 文件名和大小写必须完全一致。
- `boot_update_request.json` 必须位于根目录。
- 三个固件文件必须位于 `firmware/` 目录。
- 不要将 ZIP 文件直接放入 SD 卡，Bootloader 不会解压缩。
- 不要混合复制不同构建批次的 Manifest、APP、GUI 或 request。

## 2. 环境准备

示例工程路径：

```text
Application：E:\prjs\cmake\stm32h7app
Bootloader： E:\prjs\cmake\stm32h7boot
SD 卡：      G:\
```

检查工具：

```powershell
python --version
cmake --version
ninja --version
```

安装升级包工具依赖：

```powershell
Set-Location E:\prjs\cmake\stm32h7boot
python -m pip install -r Tools\UpgradePackage\requirements.txt
```

## 3. 版本规划

制作前确定以下版本参数：

| 参数 | 示例 | 说明 |
|---|---|---|
| Package ID | `hmi-1.0.2+20260810` | 本次发布包的唯一标识 |
| Release Version | `1.0.2` | 必须严格高于设备当前版本 |
| Build Number | `20260810` | 构建编号，建议使用日期 |
| Minimum Bootloader | `1.0.0` | 运行该包要求的最低 Bootloader 版本 |

Bootloader 的升级策略只比较 `major.minor.patch`。仅增加 `build-number` 不会被判定为新版本。例如设备当前为 `1.0.1`，候选包必须至少为 `1.0.2`。

## 4. 编译 Application

开发调试示例使用 Debug 构建：

```powershell
$AppRoot = "E:\prjs\cmake\stm32h7app"
$AppElf = "$AppRoot\build\Debug\stm32h7app.elf"

Set-Location $AppRoot
cmake --preset Debug
cmake --build --preset Debug

if (-not (Test-Path -LiteralPath $AppElf)) {
    throw "Application ELF 未生成：$AppElf"
}

Get-Item -LiteralPath $AppElf |
    Select-Object FullName,Length,LastWriteTime
```

正式发布应使用已经评审确认的构建类型和 ELF。打包工具要求 ELF 中存在以下物理加载区域：

```text
APP：0x90000000 .. 0x900FFFFF，最大 1 MiB
GUI：0x90200000 .. 0x909FFFFF，最大 8 MiB
```

## 5. 创建全新发布目录

每次发布使用新的目录，避免遗留旧 request 或旧二进制：

```powershell
$BootRoot = "E:\prjs\cmake\stm32h7boot"
$ReleaseRoot = "C:\Users\eqcn\Desktop\release-hmi-1.0.2-20260810"

if (Test-Path -LiteralPath $ReleaseRoot) {
    throw "发布目录已存在，请使用新的版本目录：$ReleaseRoot"
}

New-Item -ItemType Directory -Path $ReleaseRoot | Out-Null
Set-Location $BootRoot
```

## 6. 从 ELF 生成升级文件

```powershell
python Tools\UpgradePackage\build_upgrade_package.py build `
  --elf $AppElf `
  --output $ReleaseRoot `
  --package-id "hmi-1.0.2+20260810" `
  --version "1.0.2" `
  --build-number 20260810 `
  --minimum-bootloader "1.0.0"
```

成功后应生成：

```text
<ReleaseRoot>/firmware/
├── manifest.json
├── hmi.app.bin
└── hmi.gui.bin
```

打包工具会根据最终 ELF 生成 APP/GUI 原始镜像，并把实际文件大小和 SHA-256 写入 `manifest.json`。

## 7. 校验发布目录

```powershell
python Tools\UpgradePackage\build_upgrade_package.py verify `
  --package-root $ReleaseRoot
```

成功输出示例：

```json
{
  "package_id": "hmi-1.0.2+20260810",
  "verified": true
}
```

只要出现以下任意错误，就不能将该包交给设备：

```text
file size does not match manifest
file SHA-256 does not match manifest
manifest target does not match this Bootloader
APP reset vector is invalid
```

## 8. 生成开发升级请求

开发测试时，可以生成 `boot_update_request.json`：

```powershell
python Tools\UpgradePackage\build_upgrade_package.py create-dev-request `
  --package-root $ReleaseRoot
```

该命令读取当前 `manifest.json` 的原始字节，计算 SHA-256，并生成与之绑定的 request。

> `create-dev-request` 是开发阶段的 trust override，不代表正式生产认证。正式产品应由受信任的发布或 provisioning 流程创建 request。

request 必须最后生成。生成 request 后如果修改、重新格式化或替换 Manifest，必须重新执行校验并重新生成 request。

## 9. 写入 SD 卡

以下示例假设 SD 卡盘符为 `G:`。操作前必须确认盘符，避免删除错误磁盘上的文件。

```powershell
$SdRoot = "G:\"

Get-PSDrive -Name G
Get-ChildItem -LiteralPath $SdRoot
```

只清除旧升级文件，不删除系统目录：

```powershell
Remove-Item -LiteralPath "G:\boot_update_request.json" `
  -Force -ErrorAction SilentlyContinue

Remove-Item -LiteralPath "G:\firmware" `
  -Recurse -Force -ErrorAction SilentlyContinue
```

先复制 `firmware/`：

```powershell
Copy-Item -LiteralPath "$ReleaseRoot\firmware" `
  -Destination "G:\firmware" `
  -Recurse
```

校验 SD 卡上的实际固件文件：

```powershell
python Tools\UpgradePackage\build_upgrade_package.py verify `
  --package-root G:\
```

开发测试时，推荐直接在 SD 卡上最后生成 request，确保它绑定复制后的 Manifest：

```powershell
python Tools\UpgradePackage\build_upgrade_package.py create-dev-request `
  --package-root G:\
```

检查最终结构：

```powershell
Get-ChildItem -LiteralPath G:\ -Recurse |
    Select-Object FullName,Length,LastWriteTime

Get-Content -LiteralPath G:\boot_update_request.json
Get-FileHash -LiteralPath G:\firmware\manifest.json -Algorithm SHA256
```

`boot_update_request.json` 中的 `manifest_sha256` 必须与 `Get-FileHash` 的结果相同，比较时可忽略字母大小写。

## 10. 安全弹出和设备升级

1. 使用 Windows“安全删除硬件”弹出 SD 卡。
2. 设备断电后插入 SD 卡。
3. 给设备上电或执行系统复位。
4. 升级过程中不要断电或拔出 SD 卡。
5. 等待 Bootloader 完成 APP、GUI 写入、回读校验和 Active Record 提交。

正常流程应依次出现类似日志：

```text
stage=update-check
stage=mount
stage=request-load
stage=prepare-start
stage=prepare-process
stage=policy
stage=install-start
stage=install-process
stage=source-app-hash
stage=source-gui-hash
stage=app-erase
stage=app-program-read/start/poll
stage=app-target-read/hash
stage=gui-erase
stage=gui-program-read/start/poll
stage=gui-target-read/hash
stage=build-record-candidate
stage=commit-start
stage=cleanup
stage=unmount
stage=reset
```

升级完成并提交后，Bootloader 会删除已消费的 `boot_update_request.json`，然后复位进入新版本。

## 11. 动态擦除策略

当前 Bootloader 不再固定擦除完整的 1 MiB APP 分区和 8 MiB GUI 分区，而是按镜像实际大小计算：

```text
擦除长度 = 向上对齐(Manifest 中的镜像大小, Flash erase_size)
```

W25Q256 当前擦除粒度为 4096 字节。例如：

```text
APP 221200 字节 -> 擦除 225280 字节，即 0x37000
GUI 943444 字节 -> 擦除 946176 字节，即 0xE7000
```

镜像长度以外的 Flash 内容不属于新 Active Record 的有效范围，不参与运行时哈希校验或启动。

## 12. 常见问题

### 12.1 SD 卡成功挂载，但随后直接启动旧程序

如果日志在 `prepare-process` 后直接进入 `unmount`，通常是以下原因：

- request 中的 `manifest_sha256` 与实际 Manifest 不一致；
- request 与 Manifest 的 `package_id` 不一致；
- Manifest 格式或目标硬件字段错误。

重新执行：

```powershell
python Tools\UpgradePackage\build_upgrade_package.py verify --package-root G:\
python Tools\UpgradePackage\build_upgrade_package.py create-dev-request --package-root G:\
```

### 12.2 进入 policy，但没有擦除和写入

候选 Release Version 必须严格高于当前 Active Record。`build-number` 更大但 `major.minor.patch` 相同，仍会被拒绝。

### 12.3 日志持续显示 program-read/start/poll

只要 `program=0x...` 持续增加，就是正常页编程。W25Q256 每页最多写入 256 字节，因此较大的 GUI 文件会产生大量循环日志。

### 12.4 日志持续显示 erase/erase-poll

只要 `erase=0x...` 每次增加 `0x1000`，就是正常的 4 KiB 扇区擦除。使用动态擦除版本后，结束值应接近 Manifest 镜像大小向上对齐后的结果，而不是固定到整个分区上限。

### 12.5 升级中途断电

不要手动删除 SD 卡上的 request。重新上电后，Bootloader 会再次读取同一升级包并执行恢复安装。APP 首次擦除开始后，旧 Runtime 可能已经不完整，必须保留完整升级介质直到安装成功。

## 13. 发布检查清单

- [ ] Application ELF 来自本次确认的构建。
- [ ] Release Version 严格高于设备当前版本。
- [ ] 使用全新发布目录，没有混入旧文件。
- [ ] `build` 命令成功。
- [ ] 发布目录 `verify` 输出 `verified: true`。
- [ ] SD 卡只包含一套固定目录结构。
- [ ] SD 卡复制后再次执行 `verify`。
- [ ] request 在所有固件文件写入和校验完成后最后生成。
- [ ] request 的 Manifest SHA-256 与 SD 卡实际 Manifest 一致。
- [ ] SD 卡已经安全弹出。
- [ ] 升级完成后观察到 commit、cleanup 和 reset。

