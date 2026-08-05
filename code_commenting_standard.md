# STM32H7 固件代码注释规范

## 1. 目的

本规范用于统一本项目 C 代码中的注释写法，使注释能够准确说明接口契约、设计原因、
硬件约束和异常处理，同时避免重复代码本身已经表达的内容。

本规范使用以下约束词：

- **必须**：代码合入前必须满足。
- **应当**：通常需要满足；不采用时应有明确理由。
- **可以**：根据可读性和维护需要选择。

## 2. 适用范围

本规范适用于以下项目自维护目录中的 `.c` 和 `.h` 文件：

```text
Application/
Services/
Interfaces/
Adapters/
Platform/
BSP/
Composition/
Shared/
Utilities/
Tests/
```

以下代码按特殊规则处理：

- `Core/`、`USB_HOST/`、`FATFS/` 中由 CubeMX 生成的文件，只能在
  `USER CODE BEGIN` 与 `USER CODE END` 之间添加或修改注释。
- `Drivers/`、`Middlewares/` 等供应商或第三方代码，原则上不修改注释；确需修改时，
  必须记录修改原因，并确保后续升级可以重新应用该修改。
- 启动文件、链接脚本和 CMake 文件可参照本规范的核心原则，但使用各自语法支持的
  注释格式。

## 3. 基本原则

### 3.1 注释说明“为什么”和“有什么约束”

注释应当补充代码无法直接表达的信息，优先说明：

- 设计选择及其原因；
- 硬件手册、芯片勘误或协议规定带来的约束；
- 接口的前置条件、后置条件和副作用；
- 时间、容量、地址、对齐和并发要求；
- 看似多余但不能删除的操作；
- 异常路径和降级策略。

不得用注释逐句翻译代码。

```c
/* 错误：重复代码含义。 */
retry_count++;

/* 正确：说明这个值的来源和目的。 */
/* The first command after memory-mapped mode may be stale; retry once. */
retry_count++;
```

### 3.2 注释必须与代码同步

- 修改行为、参数、返回值、单位、时序或边界条件时，必须同步修改相关注释。
- 已失效的注释必须删除，不得保留“以后再改”的错误说明。
- 代码评审时，错误注释按代码缺陷处理。

### 3.3 优先让代码自解释

- 能通过准确命名、常量、枚举或辅助函数表达的信息，不应只写在注释里。
- 不得用注释掩盖过长函数、含义不明的变量名或重复逻辑。
- 魔数必须优先提取为具名常量；注释用于说明数值来源，而不是替代命名。

## 4. 语言与格式

### 4.1 语言

- 源代码中的新增注释必须使用英文，以保持与 HAL、CMSIS、FatFs 和现有日志模块一致。
- 面向团队的 Markdown 设计文档可以使用中文。
- 标识符、寄存器名、协议字段名和数据手册术语必须保留其官方英文写法。
- 同一个注释块内不得中英文混写，引用官方术语除外。

### 4.2 注释形式

- 对外接口使用 Doxygen 块注释：`/** ... */`。
- 实现说明使用普通块注释：`/* ... */`。
- 单行实现说明可以使用 `/* ... */`；项目代码不使用 `//`。
- 注释应放在被说明的声明、语句或代码块之前。
- 行尾注释仅用于简短、规则一致的字段说明，不得承载重要约束。
- 注释文本按 `.clang-format` 的 `ColumnLimit: 100` 控制，单行不超过 100 列。
- 完整句子首字母大写并以句号结束；短字段标签可以省略句号。

```c
/* Wait for the flash to leave the busy state before issuing a new command. */
```

## 5. 文件注释

项目自维护的公共头文件、内部头文件和源文件必须包含文件注释，放在文件第一行：

```c
/**
 * @file platform_cache.c
 * @brief STM32H7 data-cache maintenance operations.
 */
```

当文件职责、依赖方向或使用限制不明显时，应当在 `@brief` 后补充说明：

```c
/**
 * @file logging.h
 * @brief Hardware-independent formatted logging macros.
 *
 * Logging is configured once by the Composition layer. Callers do not depend
 * on the UART, HAL, or clock provider.
 */
```

文件注释不得包含作者、创建日期、修改记录或版本号。这些信息由 Git 管理。版权或许可
声明按项目许可证要求处理，不属于普通文件注释。

## 6. 接口注释

### 6.1 公共 API

暴露在 `include/` 目录中的公共函数必须使用 Doxygen 注释。注释至少说明接口用途；存在
参数、返回值、限制或副作用时，必须分别说明。

推荐顺序如下：

```c
/**
 * @brief Read bytes from a block device.
 *
 * The requested range must be within the device capacity. This operation may
 * block while the underlying device is busy.
 *
 * @param[in] device Initialized block-device instance.
 * @param[in] address Byte offset from the beginning of the device.
 * @param[out] data Destination buffer receiving exactly @p size bytes.
 * @param[in] size Number of bytes to read; must be greater than zero.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if an argument or range is invalid.
 * @return FIRMWARE_STATUS_IO_ERROR if the device operation fails.
 *
 * @pre @p device remains valid for the duration of the call.
 * @note This function is not safe to call from an interrupt handler.
 */
firmware_status_t BlockDevice_Read(
    const block_device_t *device,
    uint32_t address,
    void *data,
    uint32_t size);
```

接口注释必须根据实际情况覆盖以下内容：

- `@param[in]`、`@param[out]` 或 `@param[in,out]` 表明数据方向；
- 指针是否允许为 `NULL`；
- 缓冲区的最小长度、数据格式、所有权和有效期；
- 数值的单位、有效范围、是否允许为零以及边界是否包含；
- 每个调用方需要处理的返回状态；
- 是否阻塞、是否可重入、是否线程安全、是否允许在 ISR 中调用；
- 对外设状态、全局状态、看门狗或持久化数据的副作用；
- DMA 缓冲区的地址范围、Cache 一致性及对齐要求；
- 必须由调用方保证的前置条件使用 `@pre`；
- 容易误用但不属于前置条件的信息使用 `@note`；
- 可能导致数据损坏、设备复位或安全问题的信息使用 `@warning`。

参数名称必须与函数声明完全一致。`@return` 必须描述语义，不得只重复 C 类型。

### 6.2 内部函数

- `static` 函数不要求逐个添加 Doxygen 注释。
- 函数名称和结构足以表达行为时，不添加注释。
- 算法、硬件时序、非显然副作用或特殊调用约束存在时，必须在函数前添加普通块注释。
- 内部函数被 Doxygen 文档收集或具有完整契约时，可以使用 Doxygen 注释。

```c
/* Convert the HAL result without exposing HAL types above the BSP layer. */
static firmware_status_t HalStatus(HAL_StatusTypeDef status);
```

## 7. 类型、字段、宏和全局数据

### 7.1 结构体与枚举

- 公共结构体和枚举必须说明整体用途。
- 含义不直观的公共字段和枚举值必须注释。
- 表示容量、地址、时间、频率或计数的字段必须标明单位。
- 位掩码必须说明每一位或给出对应的数据手册章节。

```c
/** Device geometry reported in bytes. */
typedef struct
{
    uint32_t capacity_bytes; /**< Total addressable capacity. */
    uint32_t write_size;     /**< Minimum programmable unit. */
    uint32_t erase_size;     /**< Minimum erasable unit. */
} block_device_info_t;
```

### 7.2 宏和常量

- 公共配置宏必须说明用途、单位、合法范围和默认行为。
- 由芯片手册、器件手册或协议计算得到的常量，必须说明来源或公式。
- 一组自解释的状态值或寄存器位定义不要求重复注释。

```c
/* FMC SDCLK is 100 MHz. Use 64 ms / 8192 rows minus the STM32H7 margin. */
#define BSP_SDRAM_REFRESH_COUNT 761U
```

### 7.3 全局和静态数据

- 可修改的全局数据必须说明所有者、初始化时机和并发访问规则。
- 文件内静态数据仅在生命周期、单位或状态转换不明显时添加注释。
- 不得用注释为无边界的共享可变状态辩护；应优先收窄作用域或封装访问。

## 8. 实现注释

以下场景必须添加实现注释：

- 操作顺序由外设时序或协议规定；
- 代码用于规避芯片勘误、编译器问题或第三方库缺陷；
- 中断、DMA、Cache、内存屏障或临界区行为不直观；
- 算法存在精度、溢出、回绕、截断或性能权衡；
- 空操作、重复读取、延时或类型转换看似可以删除；
- 失败后刻意忽略错误、继续运行或执行降级路径。

硬件相关注释应尽可能引用稳定来源：

```c
/* RM0433, QUADSPI functional description: clear TCF before a new transfer. */
```

引用来源时使用文档编号、外设章节或勘误编号，不使用容易失效的网页链接代替核心说明。

复杂流程可在代码块前写简短的步骤或不变量，但不得写成长篇设计文档。超过约 10 行的
背景说明应移到 Markdown 设计文档，并在代码中引用文档路径或章节。

## 9. 特殊上下文要求

### 9.1 中断和并发

中断服务函数、回调函数和跨上下文共享状态必须说明：

- 运行上下文（ISR、主循环或任务）；
- 能否阻塞以及允许调用的 API 类型；
- 与其他上下文共享的数据和同步方式；
- 执行时间或延迟方面的重要约束。

### 9.2 DMA 与 Cache

涉及 STM32H7 DMA 或 D-Cache 的接口和缓冲区必须说明：

- 地址和长度的 Cache Line 对齐要求；
- 调用前后由谁执行 Clean 或 Invalidate；
- 缓冲区所在内存区域是否可被目标 DMA 访问；
- DMA 完成前缓冲区的所有权和禁止访问规则。

### 9.3 时间与回绕

使用 tick、超时或延时的代码必须注明单位。依赖无符号整数回绕计算超时的非显然逻辑，
必须说明回绕是预期行为。

```c
/* Unsigned subtraction keeps the timeout check valid across tick wraparound. */
if ((uint32_t)(now_ms - start_ms) >= timeout_ms)
{
    return FIRMWARE_STATUS_TIMEOUT;
}
```

## 10. 待办与临时措施

待办标记统一使用以下格式：

```c
/* TODO(#123): Replace polling after the DMA completion path is available. */
/* FIXME(#456): Prevent address overflow before enabling large flash devices. */
```

- `TODO` 表示已知且计划完成的工作。
- `FIXME` 表示已知缺陷或风险。
- 括号中必须填写可追踪的 issue、任务或缺陷编号。
- 注释必须说明要解决的问题或完成条件，不得只写人名或日期。
- 临时代码不得使用 `TODO` 代替必要的错误处理。

## 11. 禁止事项

不得提交以下注释：

- 注释掉的旧代码；历史实现应从 Git 查询；
- 与实际行为不一致或无法验证的描述；
- `increment counter`、`call init` 等逐句翻译代码的注释；
- 作者名、修改日期、手工维护的版本历史；
- 无任务编号的 `TODO`、`FIXME`、`HACK` 或 `XXX`；
- 将密钥、口令、内部服务器地址或其他敏感信息写入注释；
- 对硬件行为只写“特殊处理”“经验值”而不说明依据；
- 大段复制数据手册或第三方文档；应概括约束并注明来源；
- 使用注释关闭静态检查告警但不说明安全依据。

## 12. 示例

### 12.1 不合格示例

```c
/* Initialize QSPI. */
status = BSP_ExternalFlashInit();

/* Delay. */
HAL_Delay(1U);
```

注释只重复函数名，且未说明延时原因和单位语义。

### 12.2 合格示例

```c
status = BSP_ExternalFlashInit();

/* The flash requires tSHSL2 before the first status-register command. */
HAL_Delay(1U);
```

### 12.3 公共 Cache 接口示例

```c
/**
 * @brief Clean D-Cache lines covering a DMA source buffer.
 *
 * @param[in] address Start of the buffer; must be 32-byte aligned.
 * @param[in] size Buffer size in bytes; must be nonzero and a multiple of 32.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if alignment or size is invalid.
 *
 * @pre The buffer is located in memory accessible to the target DMA engine.
 * @note The caller retains ownership of the buffer after this call.
 */
firmware_status_t Platform_DCacheClean(const void *address, size_t size);
```

## 13. 代码评审检查表

提交代码前，作者和评审者必须确认：

- [ ] 新增或修改的公共 API 具有准确的 Doxygen 契约。
- [ ] 参数方向、空指针规则、长度、单位、范围和所有权已说明。
- [ ] 返回状态与实现一致，调用方需要处理的错误未遗漏。
- [ ] 阻塞、ISR、可重入、线程安全和副作用要求已说明。
- [ ] DMA、Cache、地址范围和对齐约束已说明。
- [ ] 硬件时序、勘误处理和魔数注明了原因或来源。
- [ ] 注释解释的是意图和约束，没有逐句翻译代码。
- [ ] 行为变更已经同步更新或删除旧注释。
- [ ] CubeMX 注释修改位于 `USER CODE` 区域，第三方代码未被无意修改。
- [ ] 不存在注释掉的旧代码、敏感信息或不可追踪的待办标记。
- [ ] 新增注释为英文，格式和行宽符合本规范。

---

本规范自纳入仓库之日起适用于新增和修改的代码。未改动的存量代码可在相关模块变更时
逐步补齐，不要求仅为补注释而进行大范围无功能修改。
