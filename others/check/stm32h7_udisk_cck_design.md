# STM32H7 UDisk 特制校验设计

## 1. 目标

在现有 Boot 升级流程中，增加一层 `udisk.cck` 绑定校验，只有“目录结构合法 + manifest 存在 + UDisk 指纹匹配”时，才允许进入升级执行。

该方案对应 `others/check/udisk_check.ps1` 生成规则，STM32 侧按同一字段顺序重建指纹串并做 SHA256 比对。

## 2. others 目录分析结论

- `others/boot/`：升级介质示例目录，包含 `/boot/manifest.json`、镜像和日志目录。
- `others/check/udisk_check.ps1`：Windows 侧生成 `udisk.cck` 的工具脚本。
- `others/check/udisk.cck`：校验文件示例，核心字段：
  - 设备字段：`USB_VID`、`USB_PID`、`USB_SERIAL`
  - 容量字段：`CAPACITY_BYTES`
  - FAT32 字段：`FAT_BYTES_PER_SECTOR`、`FAT_SECTORS_PER_CLUSTER`、`FAT_RESERVED_SECTORS`、`FAT_NUM_FATS`、`FAT_SIZE_32`、`FAT_ROOT_CLUSTER`、`FAT_VOLUME_ID`、`FAT_FS_TYPE`
  - 摘要字段：`UNIQUE_CODE_SHA256`

## 3. STM32H7 侧实现方案

### 3.1 接入点

接入 `Boot_Usb_PollForUpgradeMedia()`，顺序为：

1. USB 枚举与挂载成功
2. `/boot` 存在
3. `/boot/manifest.json` 存在
4. `Boot_Udisk_Check()` 通过
5. 返回 `BOOT_USB_SCAN_UPGRADE_READY`

任一步失败均不进入升级执行态。

### 3.2 新增模块

- `Boot/Inc/boot_udisk_check.h`
- `Boot/Src/boot_udisk_check.c`

职责：

1. 读取并解析 `/udisk.cck`
2. 采集当前 UDisk 实际参数
3. 比对关键字段
4. 重建指纹串并计算 SHA256
5. 与 `UNIQUE_CODE_SHA256` 比对

### 3.3 实际参数采集来源

- VID/PID/Serial：
  - 通过 `USB_HOST` 模块导出接口获取
  - 在 `HOST_USER_CLASS_ACTIVE` 回调中缓存设备身份
- 容量：
  - `USB_HOST_GetLunInfo()` -> `MSC_LUNTypeDef.capacity`
  - 计算 `CAPACITY_BYTES = (block_nbr + 1) * block_size`
- FAT32 Boot Sector：
  - 使用 `disk_read()` 读取 `USBHFatFS.volbase` 扇区
  - 按 BPB 偏移读取字段（与脚本一致）

### 3.4 SHA256 串规则

STM32 侧按脚本同序拼接：

`USB_VID|USB_PID|USB_SERIAL|CAPACITY_BYTES|FAT_BYTES_PER_SECTOR|FAT_SECTORS_PER_CLUSTER|FAT_RESERVED_SECTORS|FAT_NUM_FATS|FAT_SIZE_32|FAT_ROOT_CLUSTER|FAT_VOLUME_ID|FAT_FS_TYPE`

再做 UTF-8 字节序列 SHA256，输出大写十六进制，与 `UNIQUE_CODE_SHA256` 比较。

## 4. 工程改动点

### 4.1 新配置项

位于 `Boot/Inc/boot_config.h`：

- `BOOT_UDISK_CHECK_ENABLE`
- `BOOT_UDISK_CHECK_PATH` (`/udisk.cck`)
- `BOOT_UDISK_CHECK_FORMAT_VERSION` (3)

### 4.2 新错误码

位于 `Boot/Inc/boot_error.h`：

- `BOOT_ERR_UDISK_CODE_NOT_FOUND`
- `BOOT_ERR_UDISK_CODE_PARSE`
- `BOOT_ERR_UDISK_NOT_FAT32`
- `BOOT_ERR_UDISK_INFO_MISMATCH`
- `BOOT_ERR_UDISK_HASH_MISMATCH`

并在 `Boot/Src/boot_error.c` 增加字符串映射。

### 4.3 USB Host 扩展

`USB_HOST/App/usb_host.h/.c` 增加：

- `USB_HOST_GetVid()`
- `USB_HOST_GetPid()`
- `USB_HOST_GetSerial()`
- `USB_HOST_GetLunInfo()`

用于 Boot 校验模块采集参数，避免直接耦合 USB Host 内部结构。

### 4.4 CMake

`Boot/CMakeLists.txt` 已将 `boot_udisk_check.c` 纳入 `boot_framework`。

## 5. 运行策略说明

- `USB_VID/PID/SERIAL` 在 `udisk.cck` 为空时，按“弱绑定”处理（参与哈希时保持空字符串），以兼容脚本在少数 Windows 设备栈下无法回溯 VID/PID 的场景。
- 其他 FAT32 与容量字段必须严格匹配。
- 只要任一关键字段不一致，升级盘判定为无效介质。

## 6. 建议测试项

1. 合法 UDisk + 正确 `udisk.cck`，应进入升级流程。
2. 修改 `UNIQUE_CODE_SHA256` 任意一位，应被拒绝。
3. 复制 `udisk.cck` 到另一 UDisk（容量或卷参数不同），应被拒绝。
4. 删除 `udisk.cck`，应被拒绝。
5. FAT32 改为 exFAT/NTFS，应被拒绝。
