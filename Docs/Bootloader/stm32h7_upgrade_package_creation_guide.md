# STM32H7 升级镜像制作指南

升级镜像不需要 ELF。用户只需准备同一目录中的：

```text
hmi.app.bin       （可选）
hmi.gui.bin       （可选）
therapy.app.bin   （可选）
manifest.json
```

`manifest.json` 由发布人员手工填写版本、目标硬件和 `components`。每个组件
的 `file`、`format` 固定，`size` 可以先写 `0` 或留空，`sha256` 可以先写空字符串，
脚本会根据实际 BIN 文件计算并回写这两个字段。空 `size` 只允许用于
`create-manifest` 输入模板；脚本输出和 `verify` 始终使用严格合法的 JSON。

示例模板：

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
      "size": 0,
      "sha256": ""
    },
    "gui": {
      "file": "hmi.gui.bin",
      "format": "raw-bin-v1",
      "size": 0,
      "sha256": ""
    },
    "therapy": {
      "file": "therapy.app.bin",
      "format": "raw-bin-v1",
      "size": 0,
      "sha256": ""
    }
  }
}
```

## 三步完成

以下命令从 Bootloader 工程根目录执行。`$PackageRoot` 可以是发布目录，
也可以直接使用本仓库的 `Tests\Temp`。

### 1. 制作 Manifest

```powershell
$PackageRoot = "G:\firmware"
python E:\prjs\cmake\stm32h7boot\Tools\UpgradePackage\build_upgrade_package.py create-manifest `
  --package-root $PackageRoot
```

脚本读取 Manifest 中声明的组件，计算对应 BIN 的字节数和 SHA-256，并原子回写
`manifest.json`。声明但找不到 BIN 的组件会被删除；因此目录中只有
`hmi.app.bin` 时，Manifest 会自动删除 `gui` 和 `therapy`。至少要保留一个组件。

### 2. 制作 request

```powershell
python E:\prjs\cmake\stm32h7boot\Tools\UpgradePackage\build_upgrade_package.py create-request `
  --package-root $PackageRoot
```

脚本先完整校验 Manifest 和所有已声明 BIN，再生成根目录的
`boot_update_request.json`。request 的 `component_mask` 根据 Manifest 的
`components` 自动计算：APP=`1`、GUI=`2`、therapy=`4`，组合时按位或运算。
request 绑定当前 Manifest 的原始字节 SHA-256，修改 Manifest 后必须重新生成。

### 3. 验证整体 SHA-256

```powershell
python E:\prjs\cmake\stm32h7boot\Tools\UpgradePackage\build_upgrade_package.py verify `
  --package-root $PackageRoot
```

验证会检查文件集合是否与 Manifest 一致、每个组件的 `size` 和 `sha256` 是否
匹配，并输出 Manifest SHA-256 和各组件 SHA-256。只有输出 `"verified": true`
时才可复制到 SD 卡。

## SD 卡目录

Bootloader 使用以下目录结构：

```text
SD 卡根目录/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin       （按 Manifest 选择）
    ├── hmi.gui.bin       （按 Manifest 选择）
    └── therapy.app.bin   （按 Manifest 选择）
```

若发布目录本身就是 `firmware` 目录，三个命令也可以直接对该目录执行；复制到
SD 卡时，将生成的 `boot_update_request.json` 放在 SD 根目录，并保持
`firmware` 下的文件与 Manifest 完全一致。
