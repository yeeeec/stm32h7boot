# UDisk Check Script

该脚本用于在 Windows 上根据当前脚本所在 U 盘的信息生成一个二进制校验文件 `o.cck`。

## 执行方式
- `powershell -ExecutionPolicy Bypass -File .\udisk_check.ps1`

## 当前版本规则

当前版本只使用以下两个 U 盘参数参与校验：

- `Serial`
- `VolumeID`

此外还会叠加一个固定的自定义特征码：

- `CustomFeatureCode`

最终生成一个 `uint32` 校验值，并写入到隐藏且只读的二进制文件 `o.cck` 中。

---

## 特点

- 只保留最简校验参数
- 输出文件名固定为 `o.cck`
- 输出文件为二进制 4 字节
- 输出文件属性为：
  - Hidden
  - ReadOnly
- 文件内容不是 UTF-8 文本，不是 ASCII 文本，而是原始 4 字节校验值

---

## 参与计算的字段

脚本当前参与计算的内容只有：

1. `Serial`
2. `VolumeID`
3. `CustomFeatureCode`

### Serial

通过当前脚本所在盘符对应的磁盘设备 `PNPDeviceID` 最后一段提取。

### VolumeID

通过 `Win32_LogicalDisk.VolumeSerialNumber` 获取。

### CustomFeatureCode

在脚本配置区中固定写死，例如：

```powershell
[uint32]$CustomFeatureCode = 0x12345678