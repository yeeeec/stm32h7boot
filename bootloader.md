# STM32H7 Bootloader 设计说明

## 1. 文档范围

本文只描述 `Boot/` 当前实际参与编译的 boot 实现，不引入 `App`、旧方案或外部假设。

当前 boot 已从“单槽直接覆盖”重构为：

- `s_BootInfo` 驱动的双槽升级
- `app.bin` 写入 inactive slot
- `pending / testing / confirm / rollback` 完整链路
- USB U 盘升级包仍使用 `manifest.json + 带 8 字节头的 crc/app.bin`

## 2. Flash 布局

当前内部 Flash 2MB 的使用方式如下：

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Boot | `0x08000000 ~ 0x0801FFFF` | 128 KB | boot 程序本体 |
| BootInfo | `0x08020000 ~ 0x0803FFFF` | 128 KB | `s_BootInfo` 持久化区 |
| Slot A | `0x08040000 ~ 0x0811FFFF` | 896 KB | App A |
| Slot B | `0x08120000 ~ 0x081FFFFF` | 896 KB | App B |

说明：

- `s_BootInfo` 没有改结构体定义。
- BootInfo 不放 EEPROM，而是放在 boot 后面的 128KB Flash 区。
- BootInfo 区内部采用 64 字节对齐的顺序记录方式做 journal 持久化，写满后整扇区擦除重写。

## 3. s_BootInfo 的作用

当前 boot 使用 `s_BootInfo` 维护双槽状态：

- `active_slot`
  - 当前正式稳定运行的槽位
- `pending_slot`
  - 下一次待试启动的槽位
- `confirmed`
  - 新固件是否已被 App 确认
- `boot_count`
  - `pending_slot` 已尝试启动的次数
- `max_boot_count`
  - 最大允许测试启动次数，默认 `3`
- `upgrade_state`
  - `IDLE / READY / TESTING / ROLLBACK / SUCCESS`
- `rollback_reason`
  - 记录最近一次回滚原因
- `version_a / version_b`
  - 记录两个槽位的版本号
- `app_a_crc / app_b_crc`
  - 记录两个槽位最近一次升级写入时的镜像 CRC
- `last_reset_reason`
  - 记录最近一次 boot 保存 BootInfo 时看到的复位标志
- `seq + crc`
  - 用于 BootInfo 自身有效性和新旧记录判定

## 4. BootInfo 持久化规则

BootInfo 由 `boot_info.c` 管理。

### 4.1 记录格式

- 每条 Flash 记录占 `64 bytes`
- 前面放完整的 `s_BootInfo`
- 剩余字节填 `0xFF`

### 4.2 启动读取

- boot 扫描整个 BootInfo 区
- 找出 CRC 正确且 `seq` 最大的一条记录作为当前有效值
- 若整区为空，则视为未初始化
- 若有内容但都无效，则视为损坏并重建默认值

### 4.3 保存策略

- 每次保存时：
  - 自动补 `magic`
  - 自动维护 `seq`
  - 自动重算结构体自身 `crc`
- 若后续还有空白记录，则追加写入
- 若 journal 已满，则先整区擦除，再从头写入最新一条

### 4.4 App 侧确认接口

当前代码已提供：

- `Boot_Info_ConfirmRunningImage()`

它的设计用途是：

- App 启动稳定后主动调用
- boot 根据 `SCB->VTOR` 判断当前运行槽位
- 将该槽位写成 `active_slot`
- 清空 `pending_slot`
- 写入：
  - `confirmed = BOOT_CONFIRMED`
  - `boot_count = 0`
  - `upgrade_state = UPGRADE_SUCCESS`
  - `rollback_reason = ROLLBACK_NONE`

boot 工程里已经实现了这个接口，但当前 boot 工程本身不会自动调用它，后续要由 App 接入。

## 5. 启动主流程

boot 入口仍由 `Boot_App_Init()` 和 `Boot_App_Process()` 驱动，但状态机已经从单槽流程调整为双槽决策。

当前状态：

1. `BOOT_APP_STATE_INIT`
2. `BOOT_APP_STATE_USB_SCAN`
3. `BOOT_APP_STATE_MANIFEST_LOAD`
4. `BOOT_APP_STATE_UPGRADE`
5. `BOOT_APP_STATE_JUMP`
6. `BOOT_APP_STATE_FATAL`

### 5.1 INIT

启动时先做这些事：

1. 初始化日志
2. 初始化 handoff
3. 初始化 USB 扫描上下文
4. 读取并清除 RCC reset flags
5. 读取或重建 BootInfo
6. 根据 `active_slot / pending_slot / boot_count / reset_flags` 计算本次启动计划

### 5.2 启动决策

boot 的优先级如下：

1. 若存在 `pending_slot`
   - 先判断是否应该继续测试该槽
   - 不进入 USB 升级扫描
2. 若没有 `pending_slot`
   - 进入 USB 升级扫描
   - 没有升级介质时跳转当前稳定槽

### 5.3 pending 槽位决策

如果 `pending_slot != SLOT_NONE`，当前代码会按顺序判断：

1. `pending_slot` 向量表是否有效
   - 无效则回滚，原因记为 `ROLLBACK_CRC_ERROR`
2. 本次 reset 是否为看门狗复位，且 `boot_count > 0`
   - 是则回滚，原因记为 `ROLLBACK_WDG_RESET`
3. `boot_count >= max_boot_count`
   - 是则回滚，原因记为 `ROLLBACK_BOOT_OVERFLOW`
4. 否则继续测试该 `pending_slot`

继续测试前，boot 会：

- `boot_count++`
- `confirmed = BOOT_NOT_CONFIRMED`
- `upgrade_state = UPGRADE_TESTING`
- 保存 BootInfo

### 5.4 回滚行为

发生回滚时：

- `pending_slot = SLOT_NONE`
- `confirmed = BOOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_ROLLBACK`
- `rollback_reason = 对应原因`
- 跳回原 `active_slot`
- 若原 `active_slot` 也无效，则尝试另一个可启动槽

### 5.5 稳定槽修正

若没有 `pending_slot`，但 `active_slot` 本身已无效：

- boot 会尝试切到另一个可启动槽
- 并把它改写为新的 `active_slot`

若两个槽都无效：

- boot 仍会进入 USB 扫描
- 允许通过 U 盘恢复
- 若没有合法升级介质，则最终进入 `FATAL`

## 6. USB 升级触发规则

USB 介质检查仍由 `boot_usb.c` 和 `boot_udisk_check.c` 实现。

只有在“当前没有 pending 测试任务”时，boot 才会扫描升级介质。

### 6.1 合法升级介质条件

必须同时满足：

1. USB Host 状态进入 `READY`
2. FATFS 挂载成功
3. U 盘通过 `/cck` 指纹校验
4. 存在 `/bin`
5. 存在 `/bin/manifest.json`

### 6.2 U 盘指纹校验

当前仍基于以下内容计算：

- USB VID
- USB PID
- USB Serial
- FAT32 Volume ID
- 固定盐值 `MySecretSalt2024`
- 根目录 `/cck` 中的 4 字节 little-endian 指纹

CRC 算法使用 `Boot_Crc32_IsoCalc()`。

## 7. manifest 规则

manifest 仍由 `boot_simple_manifest.c` 手写解析，当前只认：

- `file`
- `size`
- `crc32`
- `version`

其中：

- `size / crc32 / version` 支持十进制或 `0x...`
- 裸数值或字符串都可以

### 7.1 当前支持的升级文件

虽然 manifest 可以列多个 operation，但当前 boot 只会选择：

- 第一个 `file == "app.bin"`
- 且 `/bin/crc/app.bin` 实际存在

### 7.2 最小示例

```json
{
  "crc_config": "0xA5A55A5A",
  "operations": [
    {
      "file": "app.bin",
      "version": "0x00010005",
      "size": "0x00040000",
      "crc32": "0x12345678"
    }
  ]
}
```

## 8. 升级包格式

当前升级包仍不是裸 `app.bin`，而是：

```text
offset 0x00 : 4 bytes magic
offset 0x04 : 4 bytes payload_crc32
offset 0x08 : payload
```

要求：

- `magic == 0xA5A55A5A`
- `payload_crc32 == manifest.crc32`
- payload 大小等于 `manifest.size`

boot 真正打开的文件路径是：

- `/bin/crc/app.bin`

## 9. 双槽升级流程

升级由 `boot_simple_upgrade.c` 执行，但写入目标已经变为 inactive slot。

### 9.1 目标槽选择

- 若当前稳定槽是 `A`，升级写入 `B`
- 若当前稳定槽是 `B`，升级写入 `A`
- 若当前没有可用稳定槽，则默认优先写 `A`

### 9.2 单次升级尝试

一次尝试包含：

1. 打开 `/bin/crc/app.bin`
2. 校验总大小
3. 校验 8 字节包头
4. 擦除目标槽全部 Flash
5. 按 `1024 bytes` 分块读取 payload
6. 分块写入目标槽
7. 每块写完立刻回读比较
8. 计算源数据 CRC 和 Flash 回读 CRC
9. 全部写完后比较：
   - `source_crc`
   - `flash_crc`
   - `manifest.crc32`
10. 再检查目标槽向量表是否有效

### 9.3 重试

- 单次升级最多重试 `3` 次
- 写的是 inactive slot，所以失败不会破坏当前 `active_slot`
- 如果 3 次都失败：
  - 不再进入 fatal
  - 直接保留旧稳定槽继续启动

### 9.4 升级成功后的 BootInfo 变化

成功写入后，boot 会：

- 更新目标槽的 `version_x`
- 更新目标槽的 `app_x_crc`
- `pending_slot = target_slot`
- `confirmed = BOOT_NOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_READY`
- `rollback_reason = ROLLBACK_NONE`

保存成功后，再进入 `JUMP`，下一跳目标变成 `pending_slot`。

## 10. 槽位跳转规则

`boot_simple_jump.c` 现在按槽工作：

- `Boot_SimpleJump_IsSlotValid(slot)`
- `Boot_SimpleJump_ToSlot(slot)`

### 10.1 合法性检查

对目标槽做如下检查：

1. 向量表首地址的 MSP 不能是 `0xFFFFFFFF`
2. ResetHandler 不能是 `0xFFFFFFFF`
3. MSP 必须 8 字节对齐
4. MSP 必须落在合法 SRAM 区
5. ResetHandler 清掉 Thumb bit 后，必须落在对应槽的 Flash 范围内

### 10.2 跳转前动作

跳转前仍会：

- 卸载 USB 文件系统
- 记录 handoff 向量信息
- 关闭中断
- 清 NVIC enable/pending
- 关闭 SysTick
- `HAL_RCC_DeInit()`
- `HAL_DeInit()`
- 关闭 I/D Cache
- 设置 `SCB->VTOR = slot_base`
- 设置 MSP/PSP/CONTROL
- 跳到该槽 ResetHandler

## 11. 打包工具

`others/process_bin.bat` 已同步到新的双槽方案。

当前工具会：

1. 读取 `bin/manifest.json`
2. 处理 `bin/<file>`
3. 自动做 4 字节对齐
4. 用 STM32 兼容的 `CRC32 MPEG-2` 算法计算 payload CRC
5. 自动更新 manifest 中的：
   - `size`
   - `crc32`
   - 若已有 `version`，则归一化成 `0xXXXXXXXX`
   - 若 `app.bin` 缺失 `version`，则自动补成 `0x00000000`
6. 生成 `bin/crc/<file>`

注意：

- 双槽由 boot 自己决定写到 A 还是 B
- 升级包本身不绑定固定槽位

## 12. 当前限制

虽然已经切到双槽，但当前实现仍有这些边界：

- 只处理 `app.bin`
- 还没有做签名校验
- 还没有版本策略拦截
- `ROLLBACK_BOOT_TIMEOUT` 常量已保留，但当前主要实际使用的是：
  - `ROLLBACK_CRC_ERROR`
  - `ROLLBACK_BOOT_OVERFLOW`
  - `ROLLBACK_WDG_RESET`
- App 侧确认接口已经有，但还需要 App 真正接入 `Boot_Info_ConfirmRunningImage()`

## 13. 结论

当前 boot 的真实行为可以概括为：

“上电后先读 BootInfo；若存在待测试的新固件，则优先决定是继续测试还是回滚；若系统当前处于稳定状态，则扫描合法升级 U 盘，把新的 `app.bin` 写入 inactive slot，并把该槽标记为 pending；随后按槽位做向量表校验并跳转。只要 App 在新槽稳定运行后调用确认接口，新的 pending 槽就会转正为 active 槽；若新槽多次启动失败、看门狗复位，或自身镜像无效，则 boot 自动回滚到旧稳定槽。” 
