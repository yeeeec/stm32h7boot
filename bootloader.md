# STM32H7 Bootloader 设计说明

## 1. 文档定位

本文档只描述当前已经接入编译、并且实际运行的 boot 设计。

判断标准只有一个：

- 以 `Boot/CMakeLists.txt` 中实际参与编译的文件为准

因此，即使 `boot_config.h` 里还保留了一些历史宏，例如 A/B、config 区、extflash 之类的定义，也不代表这些能力仍然有效。当前 boot 已经被收缩为“只处理 `app.bin` 的最小升级链路”。

## 2. 当前设计目标

当前 boot 的目标非常单一：

1. 上电后等待 USB U 盘枚举
2. 判断 U 盘是否为特制 U 盘
3. 检查 `/boot/manifest.json`
4. 从 manifest 中按顺序找到第一个可执行的 `app.bin`
5. 从 `/boot/crc/app.bin` 执行升级
6. 升级成功后直接跳转 APP
7. 升级连续失败 3 次后停止一切业务，只进入喂狗死循环

当前明确不做的内容：

- A/B 双分区
- pending/confirm/rollback
- recovery 模式
- 控制块读写
- config 升级
- extflash/resource 升级
- 签名校验
- 版本策略

## 3. 当前参与编译的模块

当前 boot 实际参与编译的源文件只有下面这些：

- `boot_app.c`
- `boot_usb.c`
- `boot_udisk_check.c`
- `boot_simple_manifest.c`
- `boot_simple_upgrade.c`
- `boot_simple_flash.c`
- `boot_simple_jump.c`
- `boot_crc32.c`
- `boot_error.c`
- `boot_log.c`

平台和日志支持模块：

- `Boot/Platform/*`
- `Boot/Logger/*`

已经删除或不再参与编译的旧设计文件，不属于当前设计的一部分。

## 4. Flash 布局

当前实际使用的地址如下：

- Boot 区：`0x08000000` ~ `0x0801FFFF`，`128 KB`
- APP 区：`0x08020000` ~ `0x080FFFFF`，`896 KB`

其中：

- Boot 固定运行在 `0x08000000`
- APP 固定写入并跳转到 `0x08020000`

boot 不再维护第二个 APP 槽位，也不再做镜像仲裁。

## 5. 启动主流程

boot 入口在 `Boot/Src/boot_app.c`，由 `Boot_App_Process()` 驱动。

当前状态机只有 6 个状态：

1. `BOOT_APP_STATE_INIT`
2. `BOOT_APP_STATE_USB_SCAN`
3. `BOOT_APP_STATE_MANIFEST_LOAD`
4. `BOOT_APP_STATE_UPGRADE`
5. `BOOT_APP_STATE_JUMP`
6. `BOOT_APP_STATE_FATAL`

实际流程如下：

### 5.1 INIT

- 初始化日志
- 初始化 USB 检测上下文
- 切换到 `USB_SCAN`

### 5.2 USB_SCAN

- 在 `600 ms` 时间窗口内等待 USB Host 进入 ready
- 如果 U 盘未就绪且超时，则直接跳转 APP
- 如果 U 盘 ready，则挂载 FATFS
- 按顺序执行：
  1. 特制 U 盘校验
  2. 检查 `/boot`
  3. 检查 `/boot/manifest.json`
- 全部通过后切换到 `MANIFEST_LOAD`
- 任意一步失败，则放弃升级并跳转 APP

### 5.3 MANIFEST_LOAD

- 读取并解析 `/boot/manifest.json`
- 按 `operations` 顺序扫描
- 找到第一个满足以下条件的操作项：
  - `file == "app.bin"`
  - `/boot/crc/app.bin` 实际存在
- 如果没找到，则直接跳转 APP
- 如果找到，则切换到 `UPGRADE`

### 5.4 UPGRADE

- 对选中的 `app.bin` 执行升级
- 成功则切换到 `JUMP`
- 失败则进入 `FATAL`

### 5.5 JUMP

- 如有需要先卸载 U 盘文件系统
- 校验 APP 向量表是否合法
- 关闭中断、SysTick、HAL 和 Cache
- 设置 `VTOR`
- 设置 `MSP`
- 跳转到 APP ResetHandler

### 5.6 FATAL

- 不再执行任何升级或跳转逻辑
- 无限循环调用 `Boot_Platform_FeedWatchdog()`

## 6. 特制 U 盘判定

特制 U 盘校验实现位于 `boot_udisk_check.c`。

### 6.1 输入信息

校验依赖以下信息：

- USB VID
- USB PID
- USB Serial
- FAT32 Volume ID
- 固定盐值 `MySecretSalt2024`

### 6.2 指纹算法

算法流程：

1. 组装字符串 `VID + PID + Serial + VolumeID + Salt`
2. 对该字符串计算 `CRC32 ISO`
3. 与 U 盘根目录 `/cck` 文件中的 4 字节 little-endian 指纹比较

### 6.3 额外规则

- U 盘必须是 FAT32
- `/cck` 文件长度必须是 4 字节
- 如果 USB Serial 以 `MSFT30` 开头，校验时会自动跳过这个前缀

只有全部通过时，当前 U 盘才会被视为合法升级介质。

## 7. U 盘目录结构

当前设计要求升级包至少包含：

```text
/cck
/boot/manifest.json
/boot/crc/app.bin
```

其中：

- `/cck` 用于特制 U 盘校验
- `/boot/manifest.json` 用于给出升级顺序和镜像元信息
- `/boot/crc/app.bin` 用于真正执行升级

## 8. manifest 解析规则

manifest 解析实现位于 `boot_simple_manifest.c`。

### 8.1 当前只支持的字段

每个 operation 当前只关心：

- `file`
- `size`
- `crc32`

其他字段会被忽略。

### 8.2 数值格式

`size` 和 `crc32` 支持：

- 十进制数字
- `"0x12345678"` 这种十六进制字符串

### 8.3 当前只支持的文件

manifest 中虽然可以出现多个 operation，但当前 boot 只认：

- `app.bin`

并且只会选择“按 manifest 顺序遇到的第一个、且实际存在于 `/boot/crc/` 下的 `app.bin`”。

### 8.4 最小示例

```json
{
  "operations": [
    {
      "file": "app.bin",
      "size": "0x00040000",
      "crc32": "0x12345678"
    }
  ]
}
```

## 9. 升级文件格式

当前升级文件不是裸 `app.bin`，而是带 8 字节包头的文件：

```text
offset 0x00 : 4 bytes magic
offset 0x04 : 4 bytes payload_crc32
offset 0x08 : payload
```

要求如下：

- `magic == 0xA5A55A5A`
- `payload_crc32 == manifest.crc32`
- `payload 大小 == manifest.size`

当前读取路径固定来自：

- `/boot/crc/app.bin`

## 10. CRC 规则

当前升级使用 `CRC32 MPEG-2`。

参数：

- 多项式：`0x04C11DB7`
- 初值：`0xFFFFFFFF`
- 不做反转

这与当前打包流程生成的 `crc/app.bin` 保持一致。

## 11. 单次升级流程

升级实现位于 `boot_simple_upgrade.c`。

一次升级尝试的流程如下：

1. 打开 `/boot/crc/app.bin`
2. 校验文件总大小是否等于 `manifest.size + 8`
3. 读取并校验 8 字节包头
4. 校验 `magic`
5. 校验包头中的 `payload_crc32` 是否等于 manifest 的 `crc32`
6. 擦除整个 APP 区
7. 每次读取 `1024` 字节 payload
8. 对读取块累计“源 CRC”
9. 将该块写入 APP Flash
10. 立刻从 Flash 回读同样大小的数据
11. 比较回读块和源块是否逐字节一致
12. 对回读块累计“Flash CRC”
13. 全部写完后再次比较：
    - `source_crc`
    - `flash_crc`
    - `manifest.crc32`
    - 包头中的 `payload_crc32`
14. 再校验写完后的 APP 向量表是否合法
15. 全部通过才算一次升级成功

## 12. 重试策略

升级失败会整包重试，最多 `3` 次。

当前可能触发失败的情况包括：

- 文件头错误
- 文件大小错误
- manifest CRC 不匹配
- Flash 擦除失败
- Flash 写入失败
- 写后回读块不一致
- 最终 Flash CRC 与源 CRC 不一致
- 写完后的 APP 向量表非法

如果 3 次都失败：

- 进入 `BOOT_APP_STATE_FATAL`
- 不再跳转 APP
- 只保留喂狗死循环

## 13. APP 跳转规则

APP 跳转实现位于 `boot_simple_jump.c`。

### 13.1 跳转前检查

boot 会先检查 APP 向量表：

- 初始 MSP 不能为 `0xFFFFFFFF`
- ResetHandler 不能为 `0xFFFFFFFF`
- MSP 必须落在合法 SRAM 区域
- ResetHandler 必须落在 APP Flash 区域

当前认可的 SRAM 区域：

- DTCM
- AXI SRAM
- SRAM D2
- SRAM D3

### 13.2 真正跳转前做的事

跳转前会执行：

1. 关中断
2. 关 SysTick
3. `HAL_RCC_DeInit()`
4. `HAL_DeInit()`
5. 关闭 I/D Cache
6. 设置 `SCB->VTOR = 0x08020000`
7. 设置 `MSP`
8. 清 `PSP`
9. 清 `CONTROL`
10. 跳转到 APP ResetHandler

## 14. 平台抽象层职责

平台抽象由 `Boot/Platform` 提供，当前承担：

- USB ready 状态获取
- FATFS 挂载和文件读取
- U 盘身份读取
- Flash 解锁、擦除、写入
- Cache 刷新
- 跳转前底层收尾
- 喂狗接口

其中：

- `Boot_Platform_FeedWatchdog()` 当前还是空实现
- 逻辑上已经保留“只喂狗”状态
- 但如果要真正接 IWDG/WWDG，还需要在平台层补上真实喂狗代码

## 15. 当前设计限制

这是一个“只针对 `app.bin` 的最小 boot 版本”，因此有以下限制：

- 只有一个 APP 区，没有回滚
- 升级时直接擦写唯一 APP 区
- 如果升级包错误或写入过程中掉电，APP 可能损坏
- manifest 虽然支持多个 operation，但当前只执行 `app.bin`
- 当前只做 CRC 校验，不做签名校验

这些限制不是遗漏，而是当前设计为了最小化实现而主动接受的取舍。

## 16. 结论

当前 boot 的真实行为可以概括成一句话：

“上电后在短窗口内查找特制 U 盘，如果发现合法 `manifest + crc/app.bin`，就按 1024 字节分块擦写唯一 APP 区，成功后直接跳 APP；如果升级连续失败 3 次，则停在只喂狗死循环。”
