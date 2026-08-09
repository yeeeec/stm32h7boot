# C / STM32 嵌入式项目注释规范

## 1. 目的

本规范用于统一 Bootloader、Firmware Services、BSP、Adapter、Application 等 C 代码的注释风格。

注释的目标不是解释每一行代码，而是保留代码本身难以完整表达的信息：

- 模块职责与边界
- API Contract
- 前置条件与后置条件
- Ownership
- 生命周期约束
- 状态机不变量
- Fail-safe / Fail-closed 设计
- 并发、异步或硬件行为约束
- 反直觉实现背后的原因
- 重要诊断语义

基本原则：

> 注释优先解释 Why / Contract / Invariant，而不是重复 What。

---

# 2. 总体原则

## 2.1 不重复代码已经清楚表达的信息

不推荐：

```c
/** Manifest 缓冲区容量。 */
uint32_t manifest_buffer_size;

/** 当前擦除偏移。 */
uint32_t erase_offset;

/** 当前阶段。 */
update_stage_t stage;
```

这些信息已经可以由变量名直接获得。

推荐：

```c
uint32_t manifest_buffer_size;
uint32_t erase_offset;
update_stage_t stage;
```

只有存在额外语义时才增加注释。

例如：

```c
/*
 * Bytes submitted to Flash but not yet committed by a successful poll.
 */
uint32_t pending_program_size;
```

这里的关键不是“编程大小”，而是：

- 数据已经提交给异步 Flash
- 但尚未被成功轮询确认
- 因此尚不能推进逻辑偏移

这是代码名称无法完全表达的状态语义，应保留。

---

# 3. 注释信息优先级

建议将注释分为四级。

## L1：API Contract

用于公开 API 或跨模块接口。

应描述：

- API 做什么
- 输入/输出参数
- 前置条件
- 后置条件
- Ownership
- 生命周期
- 返回值语义
- 是否允许重复调用
- 是否可能异步完成

示例：

```c
/**
 * @brief 初始化 Update Service。
 *
 * 校验所有依赖、Runtime 分区布局、Flash 几何以及调用者提供的工作缓冲区。
 * 只有全部检查成功后才修改 service 对象。
 *
 * @param[out] service
 *     调用者静态分配的服务实例。
 *
 * @param[in] dependencies
 *     服务依赖及调用者持有的工作缓冲区。
 *
 * @return
 *     FIRMWARE_STATUS_OK 初始化成功。
 *     其他状态表示参数、布局、Flash 几何或缓冲区约束失败。
 */
firmware_status_t UpdateService_Init(
    update_service_t *service,
    const update_service_dependencies_t *dependencies);
```

---

## L2：Architecture / Lifecycle

描述模块职责和层间边界。

例如：

```c
/*
 * Prepare validates package integrity and trusted-request binding only.
 * Version policy remains an Application responsibility.
 */
```

或者：

```c
/*
 * Installation success does not activate the new Runtime.
 * BootControl/Application performs the final persistent commit.
 */
```

这类注释非常重要，因为代码本身通常只能表现“调用了什么”，不能完整表达“谁应该负责什么”。

---

## L3：Safety Invariant

用于描述安全性、不变量、破坏性操作边界和 Fail-closed 行为。

例如：

```c
/*
 * Runtime must not be modified until Manifest, APP and GUI source
 * verification have all succeeded.
 */
```

```c
/*
 * Sticky once the first APP erase starts.
 * From this point the Runtime can no longer be assumed bootable.
 */
int runtime_may_be_modified;
```

```c
/*
 * source_file_open reflects actual PackageSource ownership.
 * Clear it only after close() succeeds.
 */
int source_file_open;
```

这类注释优先级最高，不应因为“精简注释”而删除。

---

## L4：Implementation Rationale

仅解释反直觉、硬件相关或容易被未来维护者“优化错”的实现。

例如：

```c
/*
 * HAL Abort may report timeout after hardware has already left
 * memory-mapped mode. The postcondition query is authoritative.
 */
service->stage = UPDATE_STAGE_XIP_VERIFY_INDIRECT;
```

这种注释的价值是防止未来维护者把后置查询删掉。

---

# 4. 文件头注释

文件头不要只描述文件名。

推荐包含：

1. 模块职责
2. 核心执行模型
3. 重要安全原则
4. 与其他层的边界

推荐：

```c
/**
 * @file update_service.c
 * @brief APP/GUI Runtime 安装状态机。
 *
 * Update Service 负责：
 *
 * - 解析并验证 Manifest；
 * - 验证 APP/GUI 源文件；
 * - 确认 QSPI 进入 indirect 模式；
 * - 擦除、编程并回读 Runtime；
 * - 生成待提交的 Active Record candidate。
 *
 * Safety rules:
 *
 * - 所有源文件完成验证后才允许首次修改 Runtime；
 * - Runtime 擦写前必须确认 QSPI 已退出 memory-mapped 模式；
 * - 每次 Process() 只推进一次有界操作；
 * - 安装成功不等于 Runtime 已激活，最终提交由 Application/BootControl 完成。
 */
```

对于复杂状态机，可以进一步集中写核心 invariant：

```c
/*
 * State-machine invariants
 * ------------------------
 *
 * 1. Runtime is immutable before all source verification succeeds.
 *
 * 2. Runtime erase/program requires positively confirmed indirect mode.
 *
 * 3. source_file_open reflects actual PackageSource ownership and is
 *    cleared only after close() succeeds.
 *
 * 4. runtime_may_be_modified is sticky after the first APP erase starts.
 *
 * 5. SUCCEEDED means installation completed, not activation committed.
 */
```

推荐将这些系统级规则集中在文件头，避免在多个函数中重复说明。

---

# 5. `.h` 与 `.c` 的注释职责

## 5.1 `.h`

`.h` 主要描述：

- 对外可见 Contract
- 数据类型语义
- 生命周期
- Ownership
- 调用约束
- 关键状态含义

不应该承担大量实现解释。

例如：

```c
typedef struct
{
    const package_source_t *package_source;
    manifest_service_t *manifest_service;
    update_request_service_t *update_request_service;
    const hash_provider_t *hash;
    const async_block_device_t *storage;
    const xip_controller_t *xip_controller;
    const boot_runtime_layout_t *runtime_layout;

    uint8_t *manifest_buffer;
    uint32_t manifest_buffer_size;

    uint8_t *io_buffer;
    uint32_t io_buffer_size;
} update_service_dependencies_t;
```

如果成员名称已经足够清楚，不需要逐成员翻译。

只有存在特殊 Contract 时增加注释。

---

## 5.2 `.c`

`.c` 主要描述：

- 为什么采用当前实现
- 状态机不变量
- 硬件行为
- 错误恢复策略
- 反直觉逻辑

推荐普通实现注释使用：

```c
/*
 * ...
 */
```

而不是所有 static 函数都使用 Doxygen：

```c
/**
 * @brief ...
 */
```

这样可以让视觉上明显区分：

- Public/API documentation
- Internal implementation notes

---

# 6. Doxygen 使用规范

建议 `/** */` 主要用于：

- Public API
- Exported type
- Public callback
- 公共模块 Contract

例如：

```c
/**
 * @brief 启动安装流程。
 *
 * @pre Prepare 已成功完成。
 * @pre Manifest 已通过 trusted request 绑定验证。
 *
 * @param[in,out] service Update Service 实例。
 *
 * @return FIRMWARE_STATUS_OK 成功进入安装状态机。
 * @return FIRMWARE_STATUS_INVALID_STATE 当前生命周期不允许安装。
 */
firmware_status_t UpdateService_InstallStart(update_service_t *service);
```

内部 static helper 推荐：

```c
/*
 * Validate the injected Runtime layout against both the frozen
 * production contract and the actual Flash geometry.
 */
static firmware_status_t ValidateRuntimeLayout(...);
```

---

# 7. 状态机枚举注释

不要机械逐条翻译枚举名称。

不推荐：

```c
/** 打开 APP 源文件。 */
UPDATE_STAGE_SOURCE_APP_OPEN,

/** 检查 APP 文件长度。 */
UPDATE_STAGE_SOURCE_APP_SIZE,

/** 计算 APP 哈希。 */
UPDATE_STAGE_SOURCE_APP_HASH,

/** 验证 APP 哈希。 */
UPDATE_STAGE_SOURCE_APP_VERIFY,
```

推荐使用分组注释：

```c
typedef enum
{
    UPDATE_STAGE_IDLE = 0,

    /* Prepare Manifest. */
    UPDATE_STAGE_PREPARE_MANIFEST_OPEN,
    UPDATE_STAGE_PREPARE_MANIFEST_SIZE,
    UPDATE_STAGE_PREPARE_MANIFEST_READ,
    UPDATE_STAGE_PREPARE_MANIFEST_CLOSE,
    UPDATE_STAGE_PREPARE_MANIFEST_PARSE,

    /*
     * Prepare completed successfully.
     * Application must accept the version policy before InstallStart().
     */
    UPDATE_STAGE_PREPARED,

    /* Ensure QSPI indirect mode. */
    UPDATE_STAGE_XIP_CHECK_INDIRECT,
    UPDATE_STAGE_XIP_EXIT,
    UPDATE_STAGE_XIP_VERIFY_INDIRECT,

    /* APP source preflight verification. */
    UPDATE_STAGE_SOURCE_APP_OPEN,
    UPDATE_STAGE_SOURCE_APP_SIZE,
    UPDATE_STAGE_SOURCE_APP_HASH,
    UPDATE_STAGE_SOURCE_APP_VERIFY,

    /* GUI source preflight verification. */
    UPDATE_STAGE_SOURCE_GUI_OPEN,
    UPDATE_STAGE_SOURCE_GUI_SIZE,
    UPDATE_STAGE_SOURCE_GUI_HASH,
    UPDATE_STAGE_SOURCE_GUI_VERIFY,

    /* APP installation. */
    UPDATE_STAGE_APP_ERASE,
    UPDATE_STAGE_APP_ERASE_POLL,
    UPDATE_STAGE_APP_PROGRAM_OPEN,
    UPDATE_STAGE_APP_PROGRAM_READ,
    UPDATE_STAGE_APP_PROGRAM_START,
    UPDATE_STAGE_APP_PROGRAM_POLL,
    UPDATE_STAGE_APP_PROGRAM_HASH,
    UPDATE_STAGE_APP_TARGET_READ,
    UPDATE_STAGE_APP_TARGET_HASH,

    /* GUI installation. */
    UPDATE_STAGE_GUI_ERASE,
    UPDATE_STAGE_GUI_ERASE_POLL,
    UPDATE_STAGE_GUI_PROGRAM_OPEN,
    UPDATE_STAGE_GUI_PROGRAM_READ,
    UPDATE_STAGE_GUI_PROGRAM_START,
    UPDATE_STAGE_GUI_PROGRAM_POLL,
    UPDATE_STAGE_GUI_PROGRAM_HASH,
    UPDATE_STAGE_GUI_TARGET_READ,
    UPDATE_STAGE_GUI_TARGET_HASH,

    /* Recovery / cancellation. */
    UPDATE_STAGE_FAILURE_CLOSE,
    UPDATE_STAGE_CANCEL_CLOSE,
    UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT,
    UPDATE_STAGE_CANCEL_XIP_EXIT,
    UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT
} update_stage_t;
```

特殊状态存在额外业务语义时，单独解释。

---

# 8. Struct 字段注释

## 8.1 不需要注释的字段

例如：

```c
uint32_t manifest_size;
uint32_t manifest_offset;
uint32_t source_offset;
uint32_t target_offset;
uint32_t erase_offset;
uint32_t program_offset;
```

字段名已经足够表达用途。

---

## 8.2 应该注释的字段

涉及：

- Ownership
- Sticky 状态
- 提交语义
- 安全语义
- 不明显生命周期
- 特殊缓存一致性

例如：

```c
/*
 * True only after Manifest syntax, integrity and trusted-request
 * binding validation have all succeeded.
 */
int manifest_ready;

/*
 * Represents actual ownership of the currently opened source file.
 * Cleared only after package_source->close() succeeds.
 */
int source_file_open;

/*
 * Sticky after the first APP erase starts.
 * Indicates that a failed installation may have left Runtime incomplete.
 */
int runtime_may_be_modified;

/*
 * Original failure stage.
 * Cleanup failures must not overwrite the root-cause diagnostic stage.
 */
update_stage_t failure_stage;

/*
 * Bytes already submitted to async Flash programming but not yet
 * committed by a successful operation result.
 */
uint32_t pending_program_size;
```

---

# 9. 行内注释规范

行内注释主要回答：

> 为什么这里必须这么做？

而不是：

> 下面一行代码正在做什么？

---

## 9.1 不推荐

```c
/* 增加擦除偏移。 */
service->erase_offset += service->storage_info.erase_size;
```

代码本身已经表达完整。

---

## 9.2 推荐

```c
/*
 * Commit the logical offset only after the device reports success.
 * On failure, program_offset must still point to the last confirmed block.
 */
service->program_offset += service->pending_program_size;
```

---

## 9.3 推荐

```c
/*
 * From the first APP erase onward the old Runtime may already be
 * unrecoverable, therefore this flag must remain sticky.
 */
service->runtime_may_be_modified = 1;
```

---

# 10. Hardware / HAL 注释

涉及 HAL、QSPI、DMA、Cache、Memory-Mapped、Flash 等硬件行为时，优先记录：

- 驱动返回值是否可靠代表硬件最终状态
- 是否需要读回确认
- Cache / Barrier 要求
- 时序约束
- 必须避免的调用方式

例如：

```c
/*
 * HAL Abort returning success is not sufficient evidence that QSPI
 * has actually left memory-mapped mode.
 *
 * Conversely, HAL may return timeout after hardware already completed
 * the transition.
 *
 * Always use is_memory_mapped() as the authoritative postcondition.
 */
```

这种注释不能简化成：

```c
/* 再次检查 QSPI。 */
```

因为后者丢失了设计原因。

---

# 11. Ownership 注释

涉及文件、锁、设备、Buffer、异步请求时，应明确 Ownership。

例如：

```c
/*
 * source_file_open is an ownership flag, not merely a cached state.
 *
 * Once open() succeeds, the Service owns the PackageSource file until
 * close() succeeds. A failed close must therefore leave this flag set.
 */
```

禁止写出会让代码状态与真实资源状态不一致的逻辑：

```c
status = source->close(...);
service->source_file_open = 0;   /* Wrong if close failed. */
```

---

# 12. Fail-closed 注释

Bootloader / Firmware Update 中存在破坏性状态时，建议明确 Fail-closed 条件。

例如：

```c
/*
 * Once Runtime mutation starts, any subsequent failure must prevent
 * normal boot unless Runtime integrity is independently re-established.
 */
```

这类注释属于系统安全 Contract，应优先于普通函数说明。

---

# 13. 异步操作注释

对于：

```text
start
poll
complete
```

模型，要明确 logical commit point。

例如：

```c
/*
 * program_start() only submits the operation.
 * program_offset is advanced only after poll/get_operation_result
 * confirms successful completion.
 */
```

不要让维护者错误地改成：

```c
program_start(...);
program_offset += chunk;
```

---

# 14. 不要在注释中制造第二个 Truth Source

对于：

- Buffer size
- Flash 地址
- 分区容量
- timeout
- retry count
- hash size
- page size
- erase size

尽量由代码常量本身表达。

不推荐：

```c
/** Manifest 最大 16 KB。 */
#define UPDATE_SERVICE_MANIFEST_MAX_SIZE 16384U
```

如果系统真实约束实际上来自：

```c
MANIFEST_SERVICE_MAX_DOCUMENT_SIZE
```

那么后续两者可能漂移。

推荐：

```c
#define UPDATE_SERVICE_MANIFEST_MAX_SIZE MANIFEST_SERVICE_MAX_DOCUMENT_SIZE
```

或者直接只使用唯一的 Contract 常量。

原则：

> 关键数值只保留一个 authoritative source。

注释描述语义，不重复容易变化的数值。

---

# 15. TODO / FIXME 规范

禁止：

```c
// TODO
```

推荐：

```c
/* TODO: Support resumable APP programming after persistent journal is available. */
```

或者：

```c
/*
 * FIXME:
 * Current adapter cannot distinguish media removal from generic I/O error.
 * Preserve BOOT_ERROR_SOURCE_IO until PackageSource exposes native reason.
 */
```

必须说明：

- 缺什么
- 为什么暂时这样
- 后续修改方向

---

# 16. 中英文使用建议

代码标识符保持英文。

注释可以采用：

- 中文说明 + 英文技术术语
- 全英文

但同一模块尽量保持一致。

推荐保留行业术语：

```text
Manifest
Runtime
Application
Service
Adapter
Provider
Ownership
Invariant
Fail-closed
XIP
memory-mapped
indirect mode
trusted request
candidate record
commit
poll
postcondition
```

不建议为了“全中文”强行翻译导致语义变模糊。

例如：

```c
/* 确认 QSPI 已退出 memory-mapped 模式。 */
```

优于：

```c
/* 确认四线串行外设接口已退出内存映射工作模式。 */
```

---

# 17. 推荐风格总结

## 应该写

### 模块级

```text
模块负责什么
模块不负责什么
生命周期
关键安全规则
状态机执行模型
```

### API

```text
Contract
precondition
postcondition
ownership
返回值语义
```

### 实现

```text
为什么
不变量
Fail-safe / Fail-closed
异步提交点
HAL 特殊行为
反直觉设计
```

---

## 不应该写

以下信息如果变量名或代码已经清楚表达，原则上删除：

```text
“当前 xxx”
“xxx 的大小”
“打开 xxx”
“关闭 xxx”
“读取 xxx”
“计算 xxx”
“进入 xxx 状态”
“增加 xxx 偏移”
```

---

# 18. Code Review 检查表

提交代码前检查：

- [ ] 文件头是否说明模块责任，而不仅是文件用途？
- [ ] 是否明确关键生命周期边界？
- [ ] 是否明确所有破坏性操作前置条件？
- [ ] 是否记录关键状态机 invariant？
- [ ] 是否记录资源 ownership？
- [ ] 是否记录 async operation 的 commit point？
- [ ] 是否记录 HAL / hardware 的反直觉行为？
- [ ] 是否存在注释只是变量名中文翻译？
- [ ] 是否存在注释只是描述下一行代码？
- [ ] 是否存在数值在代码和注释中重复维护？
- [ ] 是否存在已经失效的注释？
- [ ] static helper 是否真的需要 Doxygen？
- [ ] 枚举是否可以通过分组替代逐项说明？
- [ ] 重要安全注释是否被大量普通注释淹没？

---

# 19. 推荐最终原则

整个项目可以统一为以下一句规则：

> **Good comments preserve design intent that cannot be reconstructed reliably from the code alone.**

对应到嵌入式 Firmware：

> 注释应该优先保留架构边界、资源所有权、状态机不变量、安全条件、硬件约束以及“为什么必须这样做”，而不是把 C 代码重新翻译成人类语言。

对于 Bootloader、Firmware Update、Flash、文件系统、异步状态机等模块尤其如此。

最终目标不是：

> 注释越多越好。

而是：

> **每一条留下来的注释都值得维护。**
