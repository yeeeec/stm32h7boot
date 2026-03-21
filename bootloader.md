# STM32H7 Bootloader 最小化设计说明

## 1. 当前目标

本次 boot 只保留最简单的一条升级链路，只处理 `app.bin`。

已经明确删除出设计范围的内容：

- A/B 双分区
- pending/confirm/rollback
- 控制块持久化
- recovery 状态机
- config/resource/extflash 升级
- 版本策略
- 签名校验

当前 boot 的唯一职责：

1. 上电后识别 USB U 盘
2. 判断是否为特制 U 盘
3. 检查 `/boot/manifest.json`
4. 检查 `/boot/crc/app.bin`
5. 按 1024 字节分块把 `app.bin` 写入 APP 区
6. 每块写后回读校验
7. 全部写完后比较源 CRC 和 Flash CRC
8. 成功就直接跳转 APP
9. 连续 3 次失败就停止一切操作，只进入喂狗死循环

## 2. Flash 布局

- Boot 区：`0x08000000` ~ `0x0801FFFF`，大小 `128 KB`
- APP 区：`0x08020000` ~ `0x080FFFFF`，大小 `896 KB`

当前 boot 固定把 `app.bin` 写到：

- `BOOT_APP_BASE = 0x08020000`

## 3. U 盘目录要求

最小升级包目录如下：

```text
/cck
/boot/manifest.json
/boot/crc/app.bin
```

说明：

- `/cck` 用于判定是否为特制 U 盘
- `/boot/manifest.json` 用于给出升级顺序和 `app.bin` 的 `size/crc32`
- `/boot/crc/app.bin` 是真正参与升级的文件

## 4. 特制 U 盘判定

当前仍然沿用已有 `cck` 机制。

校验输入：

- USB VID
- USB PID
- USB Serial
- FAT32 Volume ID
- 固定盐值 `MySecretSalt2024`

校验方法：

- 按 `VID + PID + Serial + VolumeID + Salt`
- 计算 CRC32 ISO
- 与根目录 `/cck` 中的 4 字节 little-endian 指纹比较

只有匹配时，才认为这是特制 U 盘。

## 5. manifest.json 要求

当前只关心 `operations` 数组中的 `app.bin`。

支持字段：

- `file`
- `size`
- `crc32`

兼容格式：

- 十进制数值
- `0x12345678` 这种十六进制字符串

最小示例：

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

当前实现会按 `operations` 的顺序扫描，找到第一个受支持且实际存在于 `/boot/crc/` 下的 `app.bin`。

## 6. app.bin 包格式

`/boot/crc/app.bin` 不是裸 bin，而是带 8 字节包头：

```text
offset 0x00 : 4 bytes magic
offset 0x04 : 4 bytes payload_crc32
offset 0x08 : payload
```

当前要求：

- `magic = 0xA5A55A5A`
- `payload_crc32` 必须与 manifest 中的 `crc32` 一致
- `payload` 大小必须与 manifest 中的 `size` 一致

CRC 算法使用：

- `CRC32 MPEG-2`
- 初值 `0xFFFFFFFF`
- 与当前打包脚本保持一致

## 7. 实际升级流程

上电后，boot 入口在 `Boot/Src/boot_app.c`。

主流程如下：

1. 初始化日志和 USB 检测
2. 在 `600 ms` 窗口内等待 U 盘 ready
3. 挂载 FATFS
4. 先检查是否为特制 U 盘
5. 再检查 `/boot`
6. 再检查 `/boot/manifest.json`
7. 解析 manifest
8. 查找 `/boot/crc/app.bin`
9. 执行升级
10. 升级成功后跳转 APP
11. 若没有合法升级介质，则直接跳转 APP

## 8. 单次升级细节

`app.bin` 升级过程如下：

1. 打开 `/boot/crc/app.bin`
2. 校验文件总大小
3. 读取前 8 字节头部
4. 校验 magic
5. 校验头部 CRC 与 manifest CRC 是否一致
6. 擦除整个 APP 区
7. 每次读取 `1024` 字节 payload
8. 对本次读取数据累计源 CRC
9. 写入 Flash
10. 立刻从 Flash 回读同样大小数据
11. 比较源块和回读块是否一致
12. 对回读数据累计 Flash CRC
13. 全部写完后比较：
    - 源 CRC
    - Flash CRC
    - manifest CRC
    - 包头 CRC
14. 如果一致，则认为升级成功

## 9. 重试策略

如果一次升级失败，会整包重试，最多 3 次。

失败条件包括：

- 文件头错误
- 文件大小不符
- 源文件 CRC 不符
- Flash 写入失败
- Flash 回读块不一致
- 最终 Flash CRC 和源 CRC 不一致
- 写完后的 APP 向量表非法

如果 3 次都失败：

- 停止任何后续操作
- 不再跳转 APP
- 只进入 `Boot_Platform_FeedWatchdog()` 死循环

注意：

- 当前平台层的 `Boot_Platform_FeedWatchdog()` 还是空实现
- 如果后续启用 IWDG/WWDG，只需要在这个接口里接上真实喂狗代码

## 10. APP 跳转判定

跳转前会检查 APP 向量表：

- MSP 不能是 `0xFFFFFFFF`
- ResetHandler 不能是 `0xFFFFFFFF`
- MSP 必须落在合法 SRAM 区间
- ResetHandler 必须落在 APP Flash 区间

通过后执行：

1. 关中断
2. 关 SysTick
3. `HAL_RCC_DeInit()`
4. `HAL_DeInit()`
5. 关闭 Cache
6. 设置 `VTOR = 0x08020000`
7. 设置 MSP
8. 跳到 APP ResetHandler

## 11. 当前编译入口

当前参与编译的最小 boot 模块只有这些：

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

旧的复杂模块虽然还在仓库里，但已经不再参与当前 boot 流程编译。

## 12. 当前风险说明

这是“只针对 app 的最小版本”，因此有一个明确代价：

- 升级时直接擦写唯一 APP 区
- 没有回滚
- 没有备用 APP

也就是说，一旦升级包本身错误或者设备在写入过程中掉电，APP 可能不可启动。

这是本次“先做最简单版本”的设计取舍。
