# STM32H7 HMI Bootloader 完整需求基线

文档状态：设计评审草案  
适用目标：STM32H743、W25Q256、AT24、SD/FatFs、Therapy MCU  
目标：定义启动、升级、APP试运行确认、失败重装、上一版本恢复以及分层架构要求。

## 1. 已确定的总体方案

1. Application负责固件包签名验证和导入NVM。
2. Bootloader不执行签名验证，只重新执行SHA-256完整性校验。
3. Application和Bootloader通过AT24中的持久Journal交换升级请求和状态。
4. 不再使用retained RAM、`.noinit` mailbox或`boot_update_request.json`。
5. NVM只保留两个固件目录：

   ```text
   /UPDATE/firmware/   新版本候选包
   /CURRENT/firmware/  当前已确认、可用于恢复的版本
   ```

6. 不使用`CURRENT_NEW`和`CURRENT_PREVIOUS`。
7. 新APP确认成功前，`CURRENT`始终保留旧版本。
8. 新APP确认成功后，Bootloader才使用`UPDATE`替换`CURRENT`。
9. `UPDATE`在新`CURRENT`复制并验证成功前不得删除。
10. 升级状态仅包含：

    ```c
    UPDATE_STATE_IDLE
    UPDATE_STATE_REQUESTED
    UPDATE_STATE_INSTALLING
    UPDATE_STATE_JUMPING
    UPDATE_STATE_FAILED
    ```

11. `JUMPING`只用于Candidate或Rollback安装后的试运行，不用于普通`IDLE`启动。
12. Candidate试运行失败达到上限后，从`CURRENT`重新安装上一版本。
13. Rollback安装或试运行也失败后进入稳定`FAILED`安全模式。

## 2. 范围

Bootloader负责：

- 读取和维护持久Journal；
- 读取Application已经导入NVM的固件包；
- 校验原始Manifest SHA-256；
- 严格解析Manifest；
- 校验产品、硬件、版本、组件和文件集合；
- 安装HMI APP、GUI资源和Therapy MCU固件；
- 在安装过程中计算组件SHA-256；
- 对写入内容执行readback校验；
- 处理安装中断；
- 对Candidate和Rollback执行试运行；
- 在Candidate无法运行时恢复`CURRENT`；
- 在Candidate确认后将`UPDATE`提交为新的`CURRENT`；
- 验证Runtime向量表并执行Jump；
- 在不可恢复故障下进入`FAILED`。

Bootloader不负责：

- 固件包签名验证；
- ECDSA、RSA或其他非对称密码算法；
- AES或其他固件加密、解密；
- USB升级包发现与导入；
- Bootloader自身在线升级；
- APP业务功能。

## 3. 安全与信任边界

### 3.1 Application责任

Application是固件包认证边界，必须：

1. 从外部升级介质读取固件包；
2. 使用产品现有机制完成签名验证；
3. 验证Manifest结构；
4. 验证Manifest声明的全部组件；
5. 只有全部验证成功后，才允许导入NVM；
6. 将完整候选包写入`/UPDATE/firmware`；
7. 对写入文件执行sync；
8. 从NVM重新读取原始`manifest.json`并计算SHA-256；
9. 将Manifest SHA-256、候选版本和`REQUESTED`写入Journal；
10. 回读并验证Journal；
11. 触发软件复位；
12. Candidate或Rollback试运行成功后执行APP健康确认。

### 3.2 Bootloader责任

Bootloader是完整性复核和安装边界，必须：

1. 读取Journal；
2. 计算NVM中原始Manifest字节的SHA-256；
3. 与Journal中的32字节摘要直接比较；
4. 严格解析Manifest；
5. 安装过程中重新计算每个组件SHA-256；
6. 将组件摘要与Manifest声明值比较；
7. 对每个写入块执行readback比较；
8. 根据Journal状态执行安装、重试、试运行或回滚。

Bootloader可以解析但不得验证Manifest签名字段。

### 3.3 当前安全模型限制

该方案信任Application已经完成身份认证。CRC32和SHA-256用于检测数据损坏，不用于抵御能够同时修改NVM和Journal的主动攻击者。

## 4. 分层架构

依赖方向必须为：

```text
Application -> Services -> Platform -> BSP/Drivers -> HAL
```

### 4.1 Application层

Application只负责：

- 初始化Platform和看门狗；
- 调用Services处理启动流程；
- 根据Service结果执行Jump、Reset或FAILED；
- 不实现升级细节。

Application不得包含：

- FatFs操作实现；
- Manifest解析；
- SHA-256实现；
- Flash或Therapy安装适配器；
- CURRENT复制实现；
- Service callback表。

### 4.2 Services层

Services负责：

- Journal状态管理；
- 固件包读取和验证；
- 版本策略；
- 镜像安装；
- CURRENT提交；
- Candidate失败后的Rollback；
- APP健康确认；
- Runtime启动前验证。

Services可以直接调用Platform，不得直接包含HAL、FatFs、BSP或设备Driver头文件。

### 4.3 Platform层

Platform只提供：

- 外部Flash能力；
- 文件存储能力；
- AT24原始非易失读写；
- Therapy MCU编程能力；
- 系统时间、复位和看门狗；
- CPU跳转；
- 日志。

Platform不得识别Manifest、Candidate、CURRENT、升级状态或回滚策略。

### 4.4 回调限制

Application与Services之间不得使用函数指针表。函数指针port只允许存在于AT24、SPI NOR和STM32 ROM Boot等可复用设备驱动边界。

## 5. 存储模型

### 5.1 固件目录

```text
/UPDATE/firmware/   Candidate：Application认证并导入的新版本
/CURRENT/firmware/  Current：最近一次已确认、可恢复的版本
```

定义：

- `UPDATE`在Candidate试运行成功前是安装源；
- `CURRENT`在Candidate试运行期间是Rollback源；
- Candidate确认后，`UPDATE`成为新`CURRENT`的事务源；
- 新`CURRENT`完整验证成功后才能删除`UPDATE`；
- Rollback成功时不得用失败的`UPDATE`替换`CURRENT`。

### 5.2 为什么不需要CURRENT_NEW

`UPDATE`本身已经是完整、经过Application认证且由Bootloader重新校验的候选包。提交CURRENT时，`UPDATE`可直接承担事务源角色。

只要满足以下条件，就不需要`CURRENT_NEW`：

1. 替换CURRENT前重新验证UPDATE；
2. 复制过程中UPDATE保持只读；
3. CURRENT验证完成前不删除UPDATE；
4. 提交完成前不清除`CURRENT_COMMIT_PENDING`；
5. 掉电后能够删除不完整CURRENT并重新从UPDATE复制。

### 5.3 为什么不需要CURRENT_PREVIOUS

Candidate确认前，旧`CURRENT`一直保留，因此它已经是上一版本。

Candidate完成APP健康确认后，项目将其视为新的有效版本。此时允许旧版本被替换，不再支持“新版本确认成功后仍回退到更早版本”。

如果产品未来要求确认后仍保留一个历史版本，则必须重新引入独立Previous目录或A/B固件槽。

### 5.4 Journal存储

Journal基线存储介质为AT24。AT24只保存控制记录，不保存固件包。

不再使用：

- retained RAM request；
- `.noinit` mailbox；
- EEPROM BootControl旧结构；
- `boot_update_request.json`。

## 6. Journal数据模型

### 6.1 状态

```c
typedef enum
{
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_REQUESTED,
    UPDATE_STATE_INSTALLING,
    UPDATE_STATE_JUMPING,
    UPDATE_STATE_FAILED
} update_state_t;
```

| 状态 | 含义 |
|---|---|
| `IDLE` | 没有待安装事务；当前Runtime已经确认健康 |
| `REQUESTED` | Application已经提交Candidate请求 |
| `INSTALLING` | 正在安装Candidate或Rollback，Runtime可能不完整 |
| `JUMPING` | Candidate或Rollback正在试运行，等待APP健康确认 |
| `FAILED` | Candidate和Rollback均无法可靠安装或运行 |

### 6.2 镜像来源

```c
typedef enum
{
    UPDATE_SOURCE_CANDIDATE = 0,
    UPDATE_SOURCE_ROLLBACK
} update_source_t;
```

- Candidate来源：`/UPDATE/firmware`；
- Rollback来源：`/CURRENT/firmware`。

`INSTALLING`和`JUMPING`必须结合source字段解释。

### 6.3 标志

至少定义：

```c
#define UPDATE_FLAG_CURRENT_COMMIT_PENDING (1UL << 0U)
```

含义：Candidate已经由APP确认健康，但尚未完成`UPDATE -> CURRENT`提交。

### 6.4 建议记录格式

```c
typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t sequence;

    uint32_t state;
    uint32_t source;
    uint32_t flags;

    uint32_t install_attempts;
    uint32_t jump_attempts;
    uint32_t commit_attempts;
    uint32_t last_error;

    uint32_t candidate_version;
    uint8_t candidate_manifest_sha256[32];
    uint8_t running_manifest_sha256[32];

    uint32_t crc32;
} update_journal_record_t;
```

跨Application和Bootloader共享的ABI必须使用固定宽度整数，不得依赖编译器枚举大小或未定义padding。

### 6.5 A/B双槽

Journal必须使用两个物理槽：

1. 启动时读取两个槽；
2. 校验magic、format version和CRC32；
3. 两个槽均有效时选择sequence较新的记录；
4. 更新时写入非当前槽；
5. 写完后回读；
6. 回读验证成功后新记录才生效；
7. 掉电不得同时破坏唯一有效记录。

## 7. 固件包要求

### 7.1 支持组件

当前Bootloader支持：

- HMI APP；
- GUI资源；
- Therapy MCU固件。

Manifest中不得包含未实现安装路径的组件。

### 7.2 精确文件集合

`/UPDATE/firmware`和`/CURRENT/firmware`验证时必须：

- 包含一个`manifest.json`；
- 包含Manifest声明的全部组件文件；
- 不得缺少文件；
- 不得包含额外文件；
- 不得包含未知子目录；
- 文件名区分大小写；
- 文件名不得包含路径穿越字符。

### 7.3 Manifest验证

Bootloader必须验证：

- 原始Manifest SHA-256与Journal匹配；
- `format_version`受支持；
- product与目标产品一致；
- hardware与目标硬件一致；
- minimum bootloader version不高于当前版本；
- package ID格式有效；
- release版本满足版本策略；
- 组件不重复；
- 组件文件名、格式、大小和目标范围有效。

Bootloader不得执行签名验证。

## 8. Application提交升级请求

Application必须按以下顺序执行：

1. 验证外部固件包签名；
2. 验证Manifest和全部组件；
3. 确认Journal为`IDLE`且没有`CURRENT_COMMIT_PENDING`；
4. 清理旧`/UPDATE/firmware`；
5. 将完整固件包复制到`/UPDATE/firmware`；
6. 对所有文件执行sync；
7. 从NVM重新读取原始Manifest；
8. 计算原始Manifest SHA-256；
9. 写入候选版本、Manifest摘要、Candidate source和`REQUESTED`；
10. 清零安装、Jump和Commit计数；
11. 使用A/B Journal原子提交；
12. 回读Journal并验证；
13. 触发软件复位。

`REQUESTED`必须最后提交。包已经写入但请求未提交时，Bootloader应继续正常启动当前APP。

## 9. Bootloader总启动流程

Bootloader启动后：

1. 初始化最小Platform能力；
2. 初始化日志；
3. 初始化看门狗；
4. 初始化Journal存储；
5. 读取并验证A/B Journal；
6. 根据状态和标志执行对应流程。

处理优先级：

```text
FAILED
  > INSTALLING
  > JUMPING
  > REQUESTED
  > IDLE + CURRENT_COMMIT_PENDING
  > 普通IDLE启动
```

## 10. IDLE处理

### 10.1 IDLE且存在CURRENT_COMMIT_PENDING

说明Candidate已经完成APP健康确认，Bootloader必须执行CURRENT提交：

1. 挂载NVM；
2. 验证`UPDATE`仍然完整；
3. 执行`UPDATE -> CURRENT`复制替换；
4. 完整验证新`CURRENT`；
5. 删除`UPDATE`；
6. 清除`CURRENT_COMMIT_PENDING`；
7. 清除Candidate事务字段；
8. 保持`IDLE`；
9. 正常启动APP。

### 10.2 普通IDLE

普通`IDLE`表示当前Runtime已经确认健康：

1. 不写`JUMPING`；
2. 不要求APP再次执行升级确认；
3. 初始化外部Flash；
4. 进入内存映射模式；
5. 验证Runtime向量表；
6. Jump当前APP。

`JUMPING`不得用于每一次普通启动，否则APP确认后的软件复位会形成循环。

### 10.3 普通IDLE的能力边界

该五状态模型只检测升级后的试运行失败，不检测已确认APP在未来普通启动中的所有故障。

如果产品要求检测每一次正常启动失败，应增加独立Boot Health机制，不得改变本需求中`JUMPING`的语义。

## 11. REQUESTED处理

当状态为`REQUESTED`：

1. 初始化并挂载NVM；
2. 验证`UPDATE`目录和Manifest；
3. 校验Manifest原始SHA-256；
4. 校验产品、硬件、版本和精确文件集合；
5. 验证`CURRENT`是完整有效的Rollback源；
6. 设置`source = CANDIDATE`；
7. 清零安装和Jump计数；
8. 持久化`INSTALLING`；
9. 开始安装Candidate。

如果在第一次破坏性擦除前验证失败：

- Runtime仍安全；
- 记录错误；
- 清理无效UPDATE；
- 恢复`IDLE`；
- 启动当前APP。

如果`CURRENT`不存在或无效，本基线默认拒绝开始Candidate安装。量产时必须预置有效Factory/CURRENT包。

## 12. INSTALLING处理

### 12.1 重试原则

启动时检测到`INSTALLING`表示上次安装异常。Bootloader必须从镜像起始位置重新安装，不做断点续传。

推荐：

```c
#define UPDATE_MAX_INSTALL_ATTEMPTS 3U
```

每次安装前：

1. 增加`install_attempts`；
2. 持久化Journal；
3. 再执行擦除。

### 12.2 安装算法

每个组件必须：

1. 检查源文件大小；
2. 初始化目标设备；
3. 擦除目标区域；
4. 分块读取源文件；
5. 同步更新SHA-256；
6. 写入目标；
7. 立即readback；
8. 使用`memcmp()`比较源块和readback块；
9. 完成后比较组件最终SHA-256；
10. 无论成功失败都关闭文件和硬件会话。

### 12.3 Candidate安装结果

Candidate安装成功：

1. 设置`source = CANDIDATE`；
2. 清零`jump_attempts`；
3. 持久化`JUMPING`；
4. 执行Trial Jump。

Candidate安装失败：

- 未达到最大次数：保持`INSTALLING`并软件复位，下次从头重装Candidate；
- 达到最大次数：设置`source = ROLLBACK`，清零安装和Jump计数，从`CURRENT`安装上一版本；
- 不得Jump部分写入的Runtime。

### 12.4 Rollback安装结果

Rollback必须恢复`CURRENT`声明的完整Package，而不是只恢复失败组件。

Rollback安装成功：

1. 设置`source = ROLLBACK`；
2. 清零`jump_attempts`；
3. 持久化`JUMPING`；
4. 执行Trial Jump。

Rollback安装达到最大失败次数后进入`FAILED`。

## 13. JUMPING处理

`JUMPING`只表示Candidate或Rollback安装后的试运行尚未得到APP确认。

推荐：

```c
#define UPDATE_MAX_JUMP_ATTEMPTS 3U
```

### 13.1 执行Trial Jump

每次Trial Jump前必须：

1. 验证Runtime向量；
2. 增加`jump_attempts`；
3. 持久化`JUMPING`；
4. 卸载不再需要的存储；
5. 执行Jump。

### 13.2 启动时再次检测到JUMPING

说明上一次Trial Jump没有得到APP健康确认。

如果未达到最大次数：

- 不重新安装；
- 重新验证Runtime；
- 再次执行Trial Jump。

Candidate达到最大Jump次数：

- 设置`source = ROLLBACK`；
- 清零安装和Jump计数；
- 持久化`INSTALLING`；
- 从`CURRENT`恢复上一版本。

Rollback达到最大Jump次数：

- 记录错误；
- 持久化`FAILED`；
- 不再自动Jump。

复位原因可用于日志，但“未完成APP确认”才是试运行失败的主要判断依据。

## 14. APP健康确认

APP只能通过受限接口确认试运行成功，例如：

```c
firmware_status_t BootUpdate_ConfirmRunning(
    const uint8_t running_manifest_sha256[32]);
```

### 14.1 确认时机

APP检测到Journal为`JUMPING`时，不得只初始化EEPROM后立即确认。

至少应完成：

- 时钟和基础内存初始化；
- SDRAM初始化；
- RTOS启动；
- 关键任务启动；
- 关键配置加载；
- 必要硬件自检；
- 健康监控建立。

完成健康条件后再初始化或访问Journal存储并执行确认。

### 14.2 Candidate确认

确认接口必须：

1. 要求当前状态为`JUMPING`；
2. 要求`source = CANDIDATE`；
3. 校验运行版本摘要与Journal一致；
4. 写入`IDLE`；
5. 设置`CURRENT_COMMIT_PENDING`；
6. 清零安装和Jump计数；
7. 原子提交Journal；
8. 触发一次软件复位。

### 14.3 Rollback确认

Rollback APP确认时：

1. 要求`source = ROLLBACK`；
2. 写入`IDLE`；
3. 不设置`CURRENT_COMMIT_PENDING`；
4. 清零安装和Jump计数；
5. 原子提交Journal；
6. 触发一次软件复位。

Bootloader随后删除失败的`UPDATE`，保留原`CURRENT`并正常启动Rollback APP。

## 15. UPDATE提交为CURRENT

### 15.1 提交前提

仅允许在以下条件全部满足时提交：

- 状态为`IDLE`；
- `CURRENT_COMMIT_PENDING`已设置；
- Candidate已经完成APP健康确认；
- `UPDATE`完整有效；
- `UPDATE` Manifest SHA-256与Journal一致。

### 15.2 提交流程

必须按以下顺序：

1. 完整验证`UPDATE`；
2. 删除旧`CURRENT`内容；
3. 创建新的`CURRENT/firmware`；
4. 从`UPDATE`复制Manifest和全部组件；
5. 对目标文件执行sync；
6. 完整验证新`CURRENT`；
7. 删除`UPDATE`；
8. 清除`CURRENT_COMMIT_PENDING`；
9. 清除Candidate事务信息；
10. 原子写入Journal；
11. 正常Jump当前APP。

不得在新CURRENT验证成功前删除UPDATE或清除提交标志。

### 15.3 掉电恢复

| 掉电位置 | 可用数据 | 下次启动处理 |
|---|---|---|
| 删除旧CURRENT前 | UPDATE与旧CURRENT都有效 | 重新执行提交 |
| 删除CURRENT过程中 | UPDATE有效，CURRENT状态未知 | 清理CURRENT并重新复制 |
| 复制CURRENT过程中 | UPDATE有效，CURRENT不完整 | 清理CURRENT并重新复制 |
| CURRENT验证完成前 | UPDATE有效 | 重新验证或重新复制 |
| CURRENT验证后、清标志前 | UPDATE与CURRENT都有效 | 验证CURRENT后完成清理 |
| 删除UPDATE后、清标志前 | CURRENT必须有效 | 验证CURRENT并清除标志 |

### 15.4 Commit失败策略

Commit失败不会立即表示Runtime损坏，因为Candidate已经完成APP健康确认。

要求：

- 保留`CURRENT_COMMIT_PENDING`；
- 保留`UPDATE`；
- 记录`last_error`和`commit_attempts`；
- 下次启动继续提交；
- 提交未完成期间禁止Application提交下一次升级。

如果产品要求优先可用性，可以在有限重试后继续运行已确认Runtime，同时保留Pending并在后续启动重试。是否允许该行为需要产品评审确认。

## 16. Runtime验证和Jump

Jump前必须验证：

- MSP为8字节对齐；
- MSP位于允许的SRAM区域；
- SRAM区域末端可作为合法初始MSP，例如`0x20020000`；
- Reset Handler设置Thumb位；
- Reset Handler位于APP执行区域；
- 向量表满足VTOR对齐要求。

Jump必须集中在`PlatformCpu_Jump()`：

1. 禁止中断；
2. 停止SysTick；
3. 禁止并清除NVIC中断；
4. 根据实机验证结果处理D-Cache和I-Cache；
5. 设置VTOR；
6. 清除BASEPRI、FAULTMASK、CONTROL和PSP；
7. 设置MSP；
8. 执行DSB和ISB；
9. 跳转Reset Handler。

Cache操作不得分散在多个模块。

## 17. FAILED处理

`FAILED`是稳定安全模式：

1. 不再自动Jump；
2. 不再自动擦除或写入Runtime；
3. 保留Journal、错误码、来源和计数器；
4. 输出日志或错误指示；
5. 保持维护或救援入口可用；
6. 如果看门狗必须启用，由Bootloader安全循环正常喂狗；
7. 只有人工操作、Factory Recovery或新的受信维护请求才能退出FAILED。

禁止在FAILED中无限执行“Jump → 看门狗复位 → Jump”。

## 18. 看门狗要求

Bootloader必须在以下长操作中定期刷新看门狗：

- 文件复制；
- SHA-256计算；
- 外部Flash擦除；
- Therapy MCU擦除和写入；
- CURRENT复制和校验。

APP试运行阶段的喂狗必须由统一健康管理控制。APP尚未满足确认条件时，不得由无关任务持续喂狗掩盖启动失败。

## 19. 错误处理

至少区分：

- Journal无效；
- NVM不可用；
- Manifest SHA-256错误；
- Manifest格式错误；
- 目标或版本不匹配；
- 文件集合错误；
- 源文件size或SHA-256错误；
- 擦除失败；
- 写入失败；
- readback失败；
- Therapy协议失败；
- CURRENT无效；
- CURRENT提交失败；
- Runtime向量无效；
- APP试运行未确认；
- Candidate回滚失败；
- Rollback运行失败。

不得将所有失败统一映射为通用I/O错误。Journal应保存最后错误类别，日志应包含状态、来源、组件和重试次数。

## 20. 非功能要求

- 关键路径不得依赖动态内存；
- 所有缓冲区必须固定大小并检查边界；
- 所有路径拼接必须检查截断；
- 地址和长度计算必须检查溢出；
- 状态转换必须先持久化，再执行破坏性或不可返回操作；
- 安装必须可从起始位置幂等重做；
- UPDATE在提交完成前必须保持只读；
- 文件名比较必须区分大小写；
- Services不得暴露HAL、FatFs或Driver类型；
- 普通IDLE快速启动不应挂载NVM文件系统；
- 存在Commit Pending时例外；
- 最长擦除、复制和校验操作必须兼容看门狗。

## 21. 建议模块

```text
Application/
├── application.c
└── boot_flow.c

Services/Boot/
├── boot_state.c
├── boot_confirmation.c
└── runtime_image.c

Services/Update/
├── update_service.c
├── package_reader.c
├── update_manifest.c
├── image_installer.c
├── current_store.c
├── update_journal.c
└── version_policy.c
```

建议删除：

- `boot_manager_io_t`；
- `update_manager_port_t`；
- `current_manager_port_t`；
- `recovery_manager_t`；
- `image_installer_port_t`；
- retained RAM mailbox；
- `BootControl_t`旧结构；
- `boot_update_request.json`解析；
- `CURRENT_NEW`和`CURRENT_PREVIOUS`相关实现；
- 重复的`Services/stm32isp`。

## 22. 状态转换汇总

```text
IDLE
 ├─ 无Pending → 普通Jump，不改变状态
 ├─ Commit Pending → UPDATE复制验证为CURRENT → 清Pending → 普通Jump
 └─ Application提交请求 → REQUESTED

REQUESTED
 ├─ 验证失败 → 删除UPDATE → IDLE
 └─ 验证成功 → INSTALLING(CANDIDATE)

INSTALLING(CANDIDATE)
 ├─ 成功 → JUMPING(CANDIDATE)
 ├─ 未到重试上限 → 复位后从头重装
 └─ 达到上限 → INSTALLING(ROLLBACK)

JUMPING(CANDIDATE)
 ├─ APP确认 → IDLE + CURRENT_COMMIT_PENDING → 软件复位
 ├─ 未到Jump上限 → 再次Trial Jump
 └─ 达到上限 → INSTALLING(ROLLBACK)

INSTALLING(ROLLBACK)
 ├─ 成功 → JUMPING(ROLLBACK)
 ├─ 未到重试上限 → 复位后从头重装
 └─ 达到上限 → FAILED

JUMPING(ROLLBACK)
 ├─ APP确认 → IDLE，无Commit Pending → 软件复位
 ├─ 未到Jump上限 → 再次Trial Jump
 └─ 达到上限 → FAILED

FAILED
 └─ 等待人工或受信救援，不自动Jump
```

## 23. 验收场景

实现至少必须通过：

1. 普通IDLE不写JUMPING，直接快速启动；
2. Application认证、导入UPDATE并提交REQUESTED；
3. Manifest SHA-256错误时在擦除前拒绝；
4. 缺失文件、额外文件和大小写不匹配时拒绝；
5. 安装过程中掉电，下次启动检测INSTALLING并从头重装；
6. Candidate安装成功后进入JUMPING；
7. Candidate健康确认后写IDLE和Commit Pending并复位；
8. Bootloader从UPDATE复制并验证CURRENT；
9. CURRENT验证成功前UPDATE始终保留；
10. CURRENT复制过程中任意位置掉电后能够从UPDATE重建；
11. CURRENT提交成功后删除UPDATE并清Pending；
12. Candidate连续安装失败达到上限后安装CURRENT；
13. Candidate连续三次未确认后安装CURRENT；
14. Rollback安装成功并确认后保留原CURRENT；
15. Rollback安装达到上限后进入FAILED；
16. Rollback连续三次未确认后进入FAILED；
17. APP不得在只初始化EEPROM后立即确认；
18. Journal任一槽写入时掉电，另一槽仍可恢复；
19. MSP等于`0x20020000`时通过合法性检查；
20. Reset Handler越界或未设置Thumb位时禁止Jump；
21. FAILED不自动Jump且不形成高速重启循环；
22. Commit Pending未完成时Application不能提交下一次更新。

## 24. 待确认设计点

1. Candidate和Rollback安装最大尝试次数是否均为3；
2. Candidate和Rollback Trial Jump最大次数是否均为3；
3. APP健康确认的具体条件和最长时间；
4. Candidate确认后是否固定执行一次软件复位完成CURRENT提交；
5. CURRENT Commit连续失败时，是停留Bootloader还是允许运行已确认Runtime并保留Pending；
6. 首次量产是否保证预置有效CURRENT/Factory Package；
7. 更新包是否必须包含APP、GUI、Therapy完整集合，还是允许部分组件包；
8. FAILED模式的受信救援入口；
9. FAILED模式是否持续喂狗常驻；
10. Cache保持开启跳转，还是关闭后由APP重新初始化；
11. Therapy失败是否始终回滚整个Package；
12. 普通IDLE启动失败是否需要独立Boot Health机制。

上述项目确认后，Journal ABI和状态机行为才能冻结。
