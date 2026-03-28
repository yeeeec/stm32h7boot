# STM32H7 Bootloader 设计说明

> 本文按当前固定方案整理：Boot 固定放内部 Flash，`bootconfig` 固定放在 Boot 之后的 128 KB 内部 Flash，APP 固定只写入外部 QSPI Flash 的 AB 槽。

## 1. 固定方案

- Boot 固定位于内部 Flash 起始地址。
- `bootconfig` 固定位于 Boot 后面的 128 KB 内部 Flash。
- APP 不再写入内部 Flash。
- APP 只允许写入外部 QSPI Flash。
- 外部 QSPI Flash 使用 AB 双槽。
- U 盘升级时始终写 inactive slot。
- 上电未升级时，先读取 `bootconfig`，再校验目标槽位镜像；CRC 不匹配则不跳转。

## 2. Flash 布局

### 2.1 内部 Flash

内部 Flash 固定布局如下：

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Boot | `0x08000000 ~ 0x0801FFFF` | 128 KB | Bootloader 本体 |
| bootconfig | `0x081E0000 ~ 0x081FFFFF` | 128 KB | 启动配置、BootInfo、journal 区 |

说明：

- `bootconfig` 就放在 Boot 后面的 128 KB。
- 内部 Flash 不再放 APP。
- 内部 Flash `0x08040000` 以后不再作为 APP 升级目标。

### 2.2 外部 QSPI Flash

APP 固定存放在外部 QSPI Flash，采用 AB 双槽：

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| APP 总区 | `0x90000000 ~ 0x900FFFFF` | 1 MB | 固定 APP 区 |
| Slot A | `0x90000000 ~ 0x9007FFFF` | 512 KB | APP1 |
| Slot B | `0x90080000 ~ 0x900FFFFF` | 512 KB | APP2 |

结论：

- APP 只在这两个外部槽位中切换。
- Boot 不再根据介质类型做分流。
- 所有升级包最终都只会落到外部 Flash 的 `Slot A` 或 `Slot B`。

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

由于 APP 位置已经固定为外部 Flash，因此 `bootconfig` 不再需要保存介质类型或 `app_region_base`。建议只保留与 AB 槽管理直接相关的信息：

- `active_slot`
  - 当前已经确认稳定运行的槽位，取值 `A` 或 `B`。
- `pending_slot`
  - 新固件写入后的待测试槽位，取值 `A`、`B` 或 `NONE`。
- `confirmed`
  - 待测试固件是否已经被 App 确认。
- `boot_count`
  - `pending_slot` 已尝试启动的次数。
- `max_boot_count`
  - 最大允许试启动次数，建议 `3`。
- `version_a / version_b`
  - A、B 槽版本号。
- `app_a_crc / app_b_crc`
  - A、B 槽对应的镜像 CRC。
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

## 6. U 盘升级时的目标槽位选择

### 6.1 升级包合法性

Boot 读取 `/bin/app.bin` 后按以下顺序检查：

1. 版本头 `magic` 正确。
2. `image_tag` 正确。
3. `raw_bin_crc32` 非零。
4. `valid_bin_size` 合法，且与实际文件大小匹配。
5. `write_address == 0x90000000`。

只要其中任意一步失败，都拒绝烧录。

### 6.2 选择 A 还是 B

由于 APP 固定在外部 Flash，Boot 的选槽逻辑也固定如下：

1. 若 `bootconfig` 不存在，或当前没有稳定可用镜像，则优先写 `Slot A`。
2. 若 `active_slot == A`，则新包写入 `Slot B`。
3. 若 `active_slot == B`，则新包写入 `Slot A`。
4. 若当前存在 `pending_slot`，则不应继续发起新的覆盖升级，应先完成确认或回滚。

也就是说：

- 当前稳定版本在哪个槽，新版本就写另一个槽。
- 当前稳定槽绝不直接覆盖。

### 6.3 烧录成功后的 bootconfig 更新

目标槽位写入成功并通过 CRC 校验后，Boot 应更新 `bootconfig`：

- 更新目标槽位版本号
- 更新目标槽位 CRC
- `pending_slot = target_slot`
- `confirmed = BOOT_NOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_READY`
- `rollback_reason = ROLLBACK_NONE`

注意：

- 此时 `active_slot` 保持不变。
- 只有待测固件运行稳定并被 App 确认后，`pending_slot` 才转正为 `active_slot`。

## 7. 上电未升级时的启动逻辑

“上电未升级”表示本次启动没有检测到合法 U 盘升级动作。此时 Boot 应按固定外部 Flash 双槽逻辑处理：

1. 读取 `bootconfig` 最新有效记录。
2. 如果存在 `pending_slot`，优先处理待测镜像。
3. 如果不存在 `pending_slot`，处理 `active_slot`。
4. 在任何跳转前，都必须先做镜像合法性和 CRC 校验。
5. CRC 不匹配则不跳转。

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

如果 `pending_slot != NONE`，表示新固件已经写入外部 Flash，但还没有正式确认。Boot 应按如下顺序处理：

1. 先校验 `pending_slot` 的向量表和 CRC。
2. 如果校验失败：
   - 认定待测镜像无效。
   - 清除 `pending_slot`。
   - 回滚到 `active_slot`。
   - 如果回滚槽也无效，则不跳转。
3. 如果最近一次复位为看门狗复位，且 `boot_count > 0`：
   - 认定待测镜像试运行失败。
   - 回滚到 `active_slot`。
4. 如果 `boot_count >= max_boot_count`：
   - 认定试启动次数超限。
   - 回滚到 `active_slot`。
5. 如果以上都通过：
   - `boot_count++`
   - `confirmed = BOOT_NOT_CONFIRMED`
   - `upgrade_state = UPGRADE_TESTING`
   - 保存 `bootconfig`
   - 跳转到 `pending_slot`

## 10. `pending_slot` 不存在时的处理

如果 `pending_slot == NONE`，说明当前没有待测试镜像，Boot 应按如下逻辑启动：

1. 先校验 `active_slot`。
2. 若 `active_slot` 合法且 CRC 匹配，则直接跳转。
3. 若 `active_slot` 非法或 CRC 不匹配，则校验另一个槽位。
4. 若另一个槽位合法：
   - 将其改写为新的 `active_slot`
   - 清空 `pending_slot`
   - `confirmed = BOOT_CONFIRMED`
   - `boot_count = 0`
   - 保存 `bootconfig`
   - 跳转到该槽位
5. 若两个槽位都不合法：
   - 不跳转
   - 停留在 Boot，等待 U 盘恢复升级

## 11. App 确认机制

新固件从 `pending_slot` 启动成功后，App 需要在确认系统已经稳定运行后主动写确认。确认成功后，`bootconfig` 状态应更新为：

- `active_slot = 当前运行槽位`
- `pending_slot = NONE`
- `confirmed = BOOT_CONFIRMED`
- `boot_count = 0`
- `upgrade_state = UPGRADE_SUCCESS`
- `rollback_reason = ROLLBACK_NONE`

只有完成这一步，新槽位才算正式转正。

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
   - 选择外部 Flash inactive slot
   - 擦除、写入、回读校验、CRC 校验
   - 更新 `bootconfig.pending_slot`
5. `JUMP`
   - 若有 `pending_slot`，优先尝试待测槽
   - 否则启动 `active_slot`
6. `RECOVERY`
   - 无合法镜像时停留在 Boot，等待恢复升级

## 13. 最终结论

本次固定方案可以概括为：

> Boot 只负责内部 Flash 的 Boot 和 `bootconfig`；所有 APP 固定只写入外部 QSPI Flash 的 AB 槽；每次升级写 inactive slot；每次跳转前都必须校验向量表和 CRC；CRC 不匹配则不跳转。

进一步展开就是：

- `bootconfig` 固定放在 Boot 之后的 128 KB：`0x081E0000 ~ 0x081FFFFF`
- APP 固定只放外部 Flash：
  - `Slot A = 0x90000000 ~ 0x9007FFFF`
  - `Slot B = 0x90080000 ~ 0x900FFFFF`
- `app.bin.write_address` 固定要求为 `0x90000000`
- 内部 Flash 不再作为 APP 升级目标
- `pending_slot` 优先级高于 `active_slot`
- 只要目标槽位 CRC 不匹配，就不允许跳转