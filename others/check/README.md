# UDisk Check Tool

用于在 **Windows 11** 上检测当前脚本所在磁盘是否为 **USB U 盘** 且文件系统为 **FAT32**，并生成一个仅基于 **STM32 可读取字段** 的校验文件 `udisk.cck`。

---

## 功能说明

脚本 `udisk_check.ps1` 会执行以下操作：

1. 检测脚本当前所在盘符
2. 判断该磁盘是否为 **USB 设备**
3. 判断文件系统是否为 **FAT32**
4. 读取 FAT32 Boot Sector 关键参数
5. 尽量追溯并读取 USB 设备描述符信息
6. 生成带时间戳的日志文件
7. 成功时生成 `udisk.cck`

---

## 生成的文件

执行成功后，会在脚本所在目录生成：

- `udisk_check_yyyyMMdd_HHmmss.log`
- `udisk.cck`

执行失败时：

- 只生成日志文件
- 不生成 `udisk.cck`

---

## 运行方式

由于部分 Windows 默认禁止直接执行 `.ps1`，请使用下面命令运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\udisk_check.ps1
```

如果当前已经在 U 盘根目录，例如 `E:\`，则直接执行上面的命令即可。

---

## 成功判定条件

必须同时满足：

- 当前脚本所在磁盘是 **USB 盘**
- 当前文件系统是 **FAT32**
- FAT32 Boot Sector 能正常读取
- 能成功生成校验串并计算 SHA256

---

## 失败情况

### 1. 当前磁盘不是 USB

例如：

- 本地硬盘
- 非 USB 存储设备
- 系统未识别为 USB 总线设备

结果：

- 记录日志
- 退出，不生成 `udisk.cck`

### 2. 文件系统不是 FAT32

例如：

- NTFS
- exFAT
- ReFS

结果：

- 记录日志
- 退出，不生成 `udisk.cck`

### 3. 启动扇区读取失败

例如：

- 无法读取 Boot Sector
- 权限问题
- 设备状态异常

结果：

- 记录日志
- 退出，不生成 `udisk.cck`

### 4. 无法追溯到 USB VID/PID 节点

有些 U 盘在 Windows 存储栈中表现为：

- `SCSI\DISK...`
- `USB Attached SCSI (UAS)`

此时脚本会尝试沿 PnP 父节点向上追溯到真正的 `USB\VID_xxxx&PID_xxxx\...` 节点。

如果仍然找不到：

- `USB_VID`
- `USB_PID`
- `USB_SERIAL`

这些字段会留空
- 脚本仍继续执行
- 但校验能力会下降

---

## `udisk.cck` 字段说明

### 参与 SHA256 校验的字段

以下字段按固定顺序拼接，并计算 `UNIQUE_CODE_SHA256`：

- `USB_VID`
- `USB_PID`
- `USB_SERIAL`
- `CAPACITY_BYTES`
- `FAT_BYTES_PER_SECTOR`
- `FAT_SECTORS_PER_CLUSTER`
- `FAT_RESERVED_SECTORS`
- `FAT_NUM_FATS`
- `FAT_SIZE_32`
- `FAT_ROOT_CLUSTER`
- `FAT_VOLUME_ID`
- `FAT_FS_TYPE`

### 仅记录、不参与 SHA256 的字段

- `USB_MANUFACTURER`
- `USB_PRODUCT`
- `FAT_VOLUME_LABEL`

---

## SHA256 拼接格式

STM32 端必须按下面完全相同的顺序拼接字符串：

```text
USB_VID=...|USB_PID=...|USB_SERIAL=...|CAPACITY_BYTES=...|FAT_BYTES_PER_SECTOR=...|FAT_SECTORS_PER_CLUSTER=...|FAT_RESERVED_SECTORS=...|FAT_NUM_FATS=...|FAT_SIZE_32=...|FAT_ROOT_CLUSTER=...|FAT_VOLUME_ID=...|FAT_FS_TYPE=FAT32
```

然后对该字符串做 **SHA256**，与 `udisk.cck` 中的：

```text
UNIQUE_CODE_SHA256=...
```

进行比较。

---

## `udisk.cck` 示例

```text
# UDisk Check Code - STM32 Stable Fields
FormatVersion=3
GenerateTime=2026-03-15 00:50:00
DriveLetter=E:
USB_VID=0781
USB_PID=5588
USB_SERIAL=MSFT30123456790871
USB_MANUFACTURER=USB Attached SCSI (UAS) Compatible Device
USB_PRODUCT=SanDisk 3.2 Gen 1
CAPACITY_BYTES=123589361664
FAT_BYTES_PER_SECTOR=512
FAT_SECTORS_PER_CLUSTER=8
FAT_RESERVED_SECTORS=4110
FAT_NUM_FATS=2
FAT_SIZE_32=2041
FAT_ROOT_CLUSTER=2
FAT_VOLUME_ID=6A47B95A
FAT_VOLUME_LABEL=
FAT_FS_TYPE=FAT32
UNIQUE_CODE_SHA256=6C1E6633C17C33A35699A2E43DADC82A926FF59DB5AE30DF114DEFEBDD0C646D
```

---

## STM32 端实现建议

STM32 作为 USB Host 时，建议按下面流程校验：

1. 枚举 USB 设备并获取：
   - VID
   - PID
   - Serial String

2. 读取 MSC 容量信息：
   - `CAPACITY_BYTES`

3. 读取 FAT32 Boot Sector 获取：
   - BytesPerSector
   - SectorsPerCluster
   - ReservedSectors
   - NumFATs
   - FATSize32
   - RootCluster
   - VolumeID
   - FsType

4. 读取 `udisk.cck`

5. 用当前 U 盘真实值重新拼接校验串

6. 计算 SHA256，与 `UNIQUE_CODE_SHA256` 比较

---

## 注意事项

### 1. 这不是强安全认证

该方案能明显提高“随便复制一个 `.cck` 文件就伪造通过”的难度，但不能等同于硬件安全认证。

原因包括：

- 普通 U 盘的 `VID/PID` 往往同型号共用
- 部分 U 盘的 `USB_SERIAL` 可能为空或不稳定
- 某些控制器可被量产工具修改
- 整盘镜像克隆仍可能复制 FAT32 结构

### 2. 最终目标应是“提高仿造成本”

该方案适合：

- 绑定指定 U 盘
- 防止普通用户手动复制 `udisk.cck`
- 提高替换 U 盘的门槛

如果需要更强认证，应考虑：

- 自定义 USB 设备
- 安全芯片
- Challenge-Response 认证机制

---

## 日志说明

日志文件会记录：

- 脚本路径
- 当前盘符
- USB 判定结果
- FAT32 判定结果
- Boot Sector 参数
- USB 追溯结果
- 最终参与校验的字符串
- SHA256 结果
- 是否成功生成 `udisk.cck`

---

## 文件建议

建议 U 盘根目录保留以下文件：

- `udisk_check.ps1`
- `README.md`
- `udisk.cck`（生成后）

---

## 版本说明

当前 `udisk.cck` 格式版本：

```text
FormatVersion=3
```

如果以后增加字段或修改拼接规则，请同步更新：

- `README.md`
- `udisk_check.ps1`
- STM32 端校验逻辑

---
