# STM32H7 Bootloader 设计说明

> 本文按当前固定方案整理：Boot 固定放内部 Flash，`bootconfig` 固定放在内部 Flash 尾部的 128 KB，APP 固定只写入外部 QSPI Flash 的 AB 槽。

## 1. 固定方案

- Boot 固定位于内部 Flash 起始地址。
- `bootconfig` 固定位于内部 Flash 尾部的 128 KB。
- APP 不再写入内部 Flash。
- APP 只允许写入外部 QSPI Flash。
- 外部 QSPI Flash 使用 `A` 运行槽 + `B` 回滚备份槽。
- U 盘升级时先执行 `A -> B` 备份，再直接改写 `A`。
- 上电未升级时，先读取 `bootconfig`，再校验目标槽位镜像；CRC 不匹配则不跳转。

## 2. Flash 布局

### 2.1 内部 Flash

内部 Flash 固定布局如下：

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Boot | `0x08000000 ~ 0x0801FFFF` | 128 KB | Bootloader 本体 |
| bootconfig | `0x081E0000 ~ 0x081FFFFF` | 128 KB | 启动配置、BootInfo、journal 区 |

说明：

- `bootconfig` 固定放在内部 Flash 尾部的 128 KB：`0x081E0000 ~ 0x081FFFFF`。
- 内部 Flash 不再放 APP。
- 内部 Flash `0x08040000` 以后不再作为 APP 升级目标。

### 2.2 外部 QSPI Flash

APP 固定存放在外部 QSPI Flash，采用 A 运行槽 + B 回滚备份槽：

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| APP 总区 | `0x90000000 ~ 0x900FFFFF` | 1 MB | 固定 APP 区 |
| Slot A | `0x90000000 ~ 0x9007FFFF` | 512 KB | 唯一运行槽 |
| Slot B | `0x90080000 ~ 0x900FFFFF` | 512 KB | 回滚备份槽 |

结论：

- Boot 永远只从 `Slot A` 跳转。
- `Slot B` 只用于保存 `Slot A` 的回滚备份，不直接启动。
- Boot 不再根据介质类型做分流。
- 新升级包在安装时直接写 `Slot A`，并要求任意时刻 `A/B` 至少有一个完整镜像。

## 3. `app.bin` 文件格式

升级文件固定为 `/bin/app.bin`，格式仍为“版本头 + 裸固件”：

```text
offset 0x00 ~ 0x5F : 96-byte version header
offset 0x60 ~ end  : raw firmware payload
```

头部关键字段如下：

```c
typedef struct __attribute__((packed)) {
    char     magic[4];          // "FWVH"
    char     project_name[32];
    char     image_tag[4];      // "FWPK"
    char     git_hash[8];
    char     build_time[20];
    uint32_t raw_bin_crc32;     // 裸固件 CRC
    uint32_t write_address;     // 固定要求为 0x90000000
    uint32_t valid_bin_size;    // 裸固件有效长度
    uint8_t  reserved[16];
} BootVersionInfoHeader;
```

本固定方案下：

- `write_address` 不再用于判断“写内部 Flash 还是外部 Flash”。
- `write_address` 仅作为合法性检查字段使用。
- Boot 只接受 `write_address == 0x90000000` 的升级包。
- 如果头中的 `write_address` 不是 `0x90000000`，则直接拒绝烧录。

## 4. bootconfig 需要保存的信息

由于 APP 位置已经固定为外部 Flash，因此 `bootconfig` 不再需要保存介质类型或 `app_region_base`。建议只保留与 A 运行 / B 回滚直接相关的信息：

- `active_slot`
  - 当前稳定运行槽位，固定为 `A`。
- `pending_slot`
  - 当 `A` 上存在“待安装完成”或“待确认”的新镜像时取值 `A`，否则为 `NONE`。
- `confirmed`
  - `A` 上待确认镜像是否已被 App 确认。
- `boot_count`
  - 待确认镜像在 `A` 上已尝试启动的次数。
- `max_boot_count`
  - 最大允许试启动次数，建议 `3`。
- `version_a / version_b`
  - `A` 当前镜像版本号与 `B` 备份镜像版本号。
- `app_a_crc / app_b_crc`
  - `A` 当前镜像与 `B` 回滚备份镜像的 CRC。
- `app_a_size / app_b_size`
  - `A` 当前镜像与 `B` 回滚备份镜像的有效长度。
- `pending_size / pending_crc`
  - 当前待安装/待确认镜像在 `A` 上应当具备的长度与 CRC。
- `last_reset_reason`
  - 最近一次复位原因。
- `seq + crc`
  - 用于 `bootconfig` journal 记录的有效性判断。

如果沿用现有 `s_BootInfo` 结构，也可以保留冗余字段，但文档语义上应视为固定外部 Flash 方案，不再解释内部 APP 区。

## 5. bootconfig 持久化位置与规则

`bootconfig` 固定在内部 Flash：

- 基址：`0x081E0000`
- 大小：`128 KB`

建议延续 journal 方式：

1. 每条记录按固定大小写入。
2. 启动时扫描整个 `bootconfig` 区。
3. 取 CRC 正确且 `seq` 最大的一条作为当前有效记录。
4. 若区域为空，则按默认值初始化。
5. 若区域非空但全部无效，则视为损坏并重建默认值。
6. 写满后先擦除整个 `bootconfig` 区，再从头写入最新记录。

若 `bootconfig` 因 CRC 损坏被重建，且当前 `Slot A` 仍可启动但 `app_a_size / app_a_crc`
缺失，Boot 会执行一次“元数据自愈”：

1. 按整槽大小 `BOOT_APP_SLOT_SIZE` 重新计算当前 `Slot A` 的 CRC。
2. 将 `app_a_size = BOOT_APP_SLOT_SIZE`、`app_a_crc = 该整槽 CRC` 写回 `bootconfig`。
3. 后续升级即可继续执行 `A -> B` 回滚备份。

说明：

- 这是一种保守恢复策略，目的是恢复“可备份、可回滚”的当前镜像字节集合。
- 下次新镜像成功安装并确认后，`app_a_size / app_a_crc` 会被新的真实镜像元数据覆盖。

## 6. U 盘升级时的目标槽位选择

### 6.1 升级包合法性

Boot 读取 `/bin/app.bin` 后按以下顺序检查：

1. 版本头 `magic` 正确。
2. `image_tag` 正确。
3. `raw_bin_crc32` 非零。
4. `valid_bin_size` 合法，且与实际文件大小匹配。
5. `write_address == 0x90000000`。

只要其中任意一步失败，都拒绝烧录。

### 6.2 升级写入策略

由于运行地址固定为 `0x90000000`，Boot 的升级策略固定如下：

1. 先校验当前 `Slot A` 的向量表与 CRC，确认当前版本完整可运行。
2. 将当前 `Slot A` 完整复制到 `Slot B`，并对 `Slot B` 做 CRC 校验。
3. 只有在 `Slot B` 已经成为完整回滚备份后，才允许开始改写 `Slot A`。
4. 新包不再先完整缓存到 `Slot B`，而是直接从升级源写入 `Slot A`。
5. 若任一步骤失败，Boot 应保持或恢复 `Slot A` 为可启动状态。

也就是说：

- `Slot A` 永远是运行目标地址。
- `Slot B` 永远保存升级前的旧版本备份。
- 任意时刻 `A/B` 至少有一个完整镜像。

### 6.3 烧录成功后的 bootconfig 更新

在开始改写 `Slot A` 之前，Boot 应先更新 `bootconfig`，记录“回滚备份已就绪”：

- `active_slot = A`
- `app_b_size / app_b_crc = 当前旧 A 的 size / crc`
- `pending_slot = A`
- `pending_size / pending_crc = 新镜像的 size / crc`
- `confirmed = BOOT_NOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_READY`
- `rollback_reason = ROLLBACK_NONE`

随后才允许从升级源直接改写 `Slot A`。

当新镜像写入 `Slot A` 成功并通过向量表 + CRC 校验后，Boot 再将 `bootconfig` 更新为：

- `app_a_size / app_a_crc = 新镜像的 size / crc`
- `pending_slot = A`
- `pending_size / pending_crc = 新镜像的 size / crc`
- `confirmed = BOOT_NOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_TESTING`
- `rollback_reason = ROLLBACK_NONE`

## 7. 上电未升级时的启动逻辑

“上电未升级”表示本次启动没有检测到合法 U 盘升级动作。此时 Boot 应按固定外部 Flash 单运行槽逻辑处理：

1. 读取 `bootconfig` 最新有效记录。
2. 如果 `pending_slot == A`，优先处理安装中/待确认的新镜像。
3. 如果 `pending_slot == NONE`，处理当前稳定 `Slot A`。
4. 在任何跳转前，都必须先做镜像合法性和 CRC 校验。
5. Boot 只允许跳转到 `Slot A`。

## 8. 跳转前的镜像校验规则

Boot 在尝试跳转前，对目标槽位执行以下校验：

1. 检查向量表：
   - MSP 不能为 `0xFFFFFFFF`
   - ResetHandler 不能为 `0xFFFFFFFF`
   - MSP 必须落在合法 SRAM 区
   - ResetHandler 去掉 Thumb 位后必须位于目标槽位范围内
2. 在目标槽位镜像中定位嵌入式版本头。
3. 从版本头中读取：
   - `valid_bin_size`
   - `raw_bin_crc32`
   - `write_address`
4. 检查 `write_address` 必须为 `0x90000000`。
5. 按 `valid_bin_size` 重新计算目标槽位镜像 CRC。
   - 计算时将镜像内嵌头中的 `raw_bin_crc32` 字段按 `0` 处理。
6. 比较以下值是否一致：
   - Flash 实际计算得到的 CRC
   - 镜像头中的 `raw_bin_crc32`
   - `bootconfig` 中该槽位记录的 CRC

只有全部通过，目标槽位才允许跳转。

结论：

- 只看向量表合法还不够。
- CRC 不匹配，一律不跳转。

## 9. `pending_slot` 存在时的处理

如果 `pending_slot == A`，表示当前正处于“安装完成前”或“待确认测试”阶段，且 `Slot B` 应保存回滚备份。Boot 应按如下顺序处理：

1. 若 `upgrade_state == UPGRADE_READY`：
   - 若 `Slot A` 已经匹配 `pending_size / pending_crc`，说明新镜像已写完但尚未转入测试状态：
     - 更新 `upgrade_state = UPGRADE_TESTING`
     - 跳转到 `Slot A`
   - 若 `Slot A` 仍匹配旧的 `app_a_size / app_a_crc`，说明安装尚未真正覆盖旧版本：
     - 清除 `pending_slot`
     - 保持当前 `Slot A`
   - 若 `Slot A` 无效但 `Slot B` 备份有效：
     - 自动执行 `B -> A` 回滚恢复
     - 清除 `pending_slot`
     - 跳转到恢复后的 `Slot A`
   - 若 `A/B` 都无效：
     - 不跳转
     - 停留在 Boot，等待恢复升级
2. 若 `upgrade_state == UPGRADE_TESTING`：
   - 先校验 `Slot A` 是否匹配 `pending_size / pending_crc`
   - 若校验失败，自动执行 `B -> A` 回滚
   - 若最近一次复位为看门狗复位，且 `boot_count > 0`，自动执行 `B -> A` 回滚
   - 若 `boot_count >= max_boot_count`，自动执行 `B -> A` 回滚
   - 若以上都通过：
     - `boot_count++`
     - 保存 `bootconfig`
     - 跳转到 `Slot A`

## 10. `pending_slot` 不存在时的处理

如果 `pending_slot == NONE`，说明当前没有待确认安装，Boot 应按如下逻辑启动：

1. 先校验 `Slot A`。
2. 若 `Slot A` 合法且 CRC 匹配，则直接跳转。
3. 若 `Slot A` 非法，但 `Slot B` 备份合法：
   - 自动执行 `B -> A` 恢复
   - 保存 `bootconfig`
   - 跳转到恢复后的 `Slot A`
4. 若 `A/B` 都不合法：
   - 不跳转
   - 停留在 Boot，等待 U 盘恢复升级

## 11. App 确认机制

新固件从 `Slot A` 启动成功后，App 需要在确认系统已经稳定运行后主动写确认。推荐直接调用 `Boot_Info_ConfirmRunningImage()`；若仅接入 handoff mailbox，则至少要上报 `BOOT_HANDOFF_STAGE_APP_READY`，供 Boot 在下一次启动时完成转正。确认成功后，`bootconfig` 状态应更新为：

- `active_slot = A`
- `pending_slot = NONE`
- `confirmed = BOOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_SUCCESS`
- `rollback_reason = ROLLBACK_NONE`
- `pending_size = 0`
- `pending_crc = 0`

只有完成这一步，新写入的 `Slot A` 才算正式转正。

## 12. 推荐主流程

建议 Boot 主状态机固定为：

1. `INIT`
   - 初始化日志、硬件、USB、复位标志
   - 读取 `bootconfig`
2. `USB_SCAN`
   - 检查是否存在合法升级 U 盘
3. `IMAGE_LOAD`
   - 读取 `/bin/app.bin`
   - 校验头部，确认 `write_address == 0x90000000`
4. `UPGRADE`
   - 校验当前 `Slot A`
   - 将 `Slot A` 复制到 `Slot B`
   - 更新 `bootconfig`，记录 `B` 为回滚备份、`A` 为安装目标
   - 直接从升级源改写 `Slot A`
   - 对新 `Slot A` 执行向量表 + CRC 校验
   - 更新 `bootconfig` 为 `UPGRADE_TESTING`
5. `JUMP`
   - 若 `pending_slot == A`，优先按待确认流程启动 `Slot A`
   - 否则启动稳定 `Slot A`
6. `RECOVERY`
   - 无合法镜像时停留在 Boot，等待恢复升级

## 13. 最终结论

本次固定方案可以概括为：

> Boot 只负责内部 Flash 的 Boot 和 `bootconfig`；APP 固定运行在外部 QSPI Flash 的 `Slot A`；每次升级先把旧 `A` 备份到 `B`，再直接改写 `A`；任意时刻 `A/B` 至少有一个完整镜像；每次跳转前都必须校验向量表和 CRC；若升级中断或新版本失效，则自动执行 `B -> A` 回滚。

进一步展开就是：

- `bootconfig` 固定放在内部 Flash 尾部的 128 KB：`0x081E0000 ~ 0x081FFFFF`
- APP 固定只放外部 Flash：
  - `Slot A = 0x90000000 ~ 0x9007FFFF`
  - `Slot B = 0x90080000 ~ 0x900FFFFF`
- `app.bin.write_address` 固定要求为 `0x90000000`
- 内部 Flash 不再作为 APP 升级目标
- Boot 永远只跳转到 `Slot A`
- `Slot B` 永远只作回滚备份，不直接跳转
- 升级时必须先完成 `A -> B` 备份，才能开始改写 `A`
- 只要目标镜像 CRC 不匹配，就不允许跳转
