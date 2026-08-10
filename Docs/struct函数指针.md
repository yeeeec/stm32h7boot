# C 语言 `struct + 函数指针 + void *context` 接口设计详解

## 1. 概述

在嵌入式 C 项目中，经常可以看到下面这种代码：

```c
typedef struct
{
    void *context;
    block_device_get_info_fn get_info;
    block_device_read_fn read;
    block_device_program_fn program;
    block_device_erase_fn erase;
} block_device_t;
```

这种设计的核心是：

> 使用 `struct + 函数指针 + void *context` 在 C 语言中构造一个抽象接口对象。

它常用于：

* HAL 抽象
* Device Driver 抽象
* BSP 接口
* 文件系统设备接口
* Flash / Block Device 接口
* 通信接口
* Bootloader
* Firmware Service
* 单元测试 Mock
* 依赖注入

其设计思想类似于 C++ 中的：

* 对象
* `this` 指针
* interface
* virtual function

但它完全使用标准 C 语言实现。

---

# 2. 基本结构

以下面的 Block Device 接口为例：

```c
typedef struct
{
    void *context;

    block_device_get_info_fn get_info;
    block_device_read_fn read;
    block_device_program_fn program;
    block_device_erase_fn erase;

} block_device_t;
```

可以理解为：

```text
block_device_t
│
├── context
│
│   └── 指向具体设备实例
│
├── get_info()
│   └── 获取设备信息
│
├── read()
│   └── 读取数据
│
├── program()
│   └── 写入数据
│
└── erase()
    └── 擦除数据
```

`block_device_t` 本身并不代表某一种具体 Flash。

它描述的是：

> 一个 Block Device 应该具备哪些能力。

例如：

```text
W25Q256
MX25L256
Internal Flash
EEPROM
RAM 模拟设备
Mock Device
```

都可以实现这个接口。

---

# 3. `typedef struct` 的作用

代码：

```c
typedef struct
{
    void *context;
    block_device_read_fn read;
} block_device_t;
```

定义了一个新的类型：

```c
block_device_t
```

之后可以：

```c
block_device_t device;
```

也可以：

```c
block_device_t flash;
```

此时：

```c
flash.context
flash.read
```

就是结构体成员。

区别在于：

```c
context
```

是普通指针。

而：

```c
read
```

是函数指针。

---

# 4. 函数指针

## 4.1 普通函数

普通函数：

```c
firmware_status_t block_read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    ...
}
```

这个函数的特征是：

```text
返回值：

firmware_status_t

参数：

void *
uint32_t
void *
uint32_t
```

---

## 4.2 定义函数指针

可以定义一个能够指向这种函数的指针：

```c
firmware_status_t (*read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
```

其中：

```c
(*read_fn)
```

表示：

> `read_fn` 是一个函数指针。

它只能指向签名兼容的函数。

例如：

```c
read_fn = w25q256_read;
```

之后可以：

```c
read_fn(context, address, data, size);
```

实际上调用的就是：

```c
w25q256_read(context, address, data, size);
```

---

# 5. 使用 `typedef` 简化函数指针

直接写：

```c
firmware_status_t (*read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
```

比较复杂。

因此通常使用 `typedef`：

```c
typedef firmware_status_t (*block_device_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
```

这样：

```c
block_device_read_fn
```

就成为一种类型。

可以直接：

```c
block_device_read_fn read;
```

相当于：

```c
firmware_status_t (*read)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
```

所以：

```c
typedef struct
{
    block_device_read_fn read;
} block_device_t;
```

本质就是：

```c
typedef struct
{
    firmware_status_t (*read)(
        void *context,
        uint32_t address,
        void *data,
        uint32_t size);
} block_device_t;
```

使用 `typedef` 后明显更加清晰。

---

# 6. `void *context` 是什么

`context` 是这种设计中非常重要的一部分：

```c
void *context;
```

它表示：

> 指向具体实现对象的通用指针。

例如 W25Q256 需要：

```c
typedef struct
{
    QSPI_HandleTypeDef *hqspi;
    uint32_t base_address;
} w25q256_context_t;
```

创建实例：

```c
static w25q256_context_t w25q256_ctx =
{
    .hqspi = &hqspi,
    .base_address = 0x90000000,
};
```

那么：

```c
context
```

可以指向：

```c
&w25q256_ctx
```

即：

```text
block_device_t
      │
      │ context
      ▼
w25q256_context_t
┌────────────────────────┐
│ hqspi                  │
│ base_address           │
└────────────────────────┘
```

---

# 7. 为什么使用 `void *`

如果接口写成：

```c
w25q256_context_t *context;
```

那么：

```c
block_device_t
```

就只能服务于 W25Q256。

但 Block Device 接口可能对应：

```text
W25Q256
MX25L256
Internal Flash
RAM Device
Mock Device
```

不同设备的 Context 完全不同。

例如：

### W25Q256

```c
typedef struct
{
    QSPI_HandleTypeDef *hqspi;
} w25q256_context_t;
```

### SPI Flash

```c
typedef struct
{
    SPI_HandleTypeDef *hspi;

    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
} spi_flash_context_t;
```

### SD

```c
typedef struct
{
    SD_HandleTypeDef *hsd;
} sd_context_t;
```

因此抽象接口不能规定 Context 的具体类型。

于是使用：

```c
void *
```

表示：

> Context 的具体类型由 Provider 自己决定。

---

# 8. 如何恢复 Context 类型

假设接口调用：

```c
device.read(
    device.context,
    address,
    buffer,
    size);
```

进入：

```c
static firmware_status_t w25q256_read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    ...
}
```

此时：

```c
context
```

实际上指向：

```c
w25q256_context_t
```

因此可以：

```c
w25q256_context_t *ctx = context;
```

完整代码：

```c
static firmware_status_t w25q256_read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    w25q256_context_t *ctx = context;

    HAL_QSPI_Receive(
        ctx->hqspi,
        data,
        ...);

    return FIRMWARE_STATUS_OK;
}
```

这样就可以访问：

```c
ctx->hqspi
ctx->base_address
```

---

# 9. 为什么函数还需要传递 `context`

调用接口时通常会看到：

```c
device.read(
    device.context,
    address,
    buffer,
    size);
```

可能会产生一个疑问：

> `read` 已经属于 `device`，为什么还需要把 `device.context` 传进去？

原因是：

> C 语言的函数指针不会自动知道自己属于哪个对象。

例如：

```c
device.read
```

本质只是一个函数地址：

```text
device.read
     │
     ▼
0x08001234
     │
     ▼
w25q256_read()
```

它不知道：

```c
device.context
```

是什么。

因此必须手动传入：

```c
device.read(
    device.context,
    ...);
```

---

# 10. `context` 类似 C++ 的 `this`

C++ 中可以：

```cpp
flash.read(address, buffer, size);
```

成员函数内部存在隐式的：

```cpp
this
```

C 语言没有这个机制。

所以 C 通常手动模拟：

```c
flash.read(
    flash.context,
    address,
    buffer,
    size);
```

可以近似理解为：

```text
C                           C++

context                     this

read 函数指针               virtual read()

block_device_t              interface / object
```

因此：

```c
obj.function(obj.context, ...);
```

可以理解成一种 C 风格的：

```cpp
obj.function(...);
```

---

# 11. 完整 Block Device 接口示例

## 11.1 定义函数类型

```c
typedef firmware_status_t (*block_device_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);

typedef firmware_status_t (*block_device_program_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);

typedef firmware_status_t (*block_device_erase_fn)(
    void *context,
    uint32_t address,
    uint32_t size);
```

---

## 11.2 定义接口

```c
typedef struct
{
    void *context;

    block_device_read_fn read;
    block_device_program_fn program;
    block_device_erase_fn erase;

} block_device_t;
```

这就是抽象接口。

---

# 12. 实现 W25Q256

首先定义设备自己的 Context：

```c
typedef struct
{
    QSPI_HandleTypeDef *hqspi;
} w25q256_t;
```

实现读取：

```c
static firmware_status_t w25q256_read(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size)
{
    w25q256_t *device = context;

    /*
     * 使用：
     *
     * device->hqspi
     *
     * 操作具体硬件。
     */

    return FIRMWARE_STATUS_OK;
}
```

实现写入：

```c
static firmware_status_t w25q256_program(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size)
{
    w25q256_t *device = context;

    ...

    return FIRMWARE_STATUS_OK;
}
```

实现擦除：

```c
static firmware_status_t w25q256_erase(
    void *context,
    uint32_t address,
    uint32_t size)
{
    w25q256_t *device = context;

    ...

    return FIRMWARE_STATUS_OK;
}
```

---

# 13. 创建具体设备对象

首先：

```c
static w25q256_t w25q256 =
{
    .hqspi = &hqspi1,
};
```

然后创建接口：

```c
static block_device_t flash =
{
    .context = &w25q256,

    .read = w25q256_read,
    .program = w25q256_program,
    .erase = w25q256_erase,
};
```

关系为：

```text
flash : block_device_t
│
├── context ────────────────┐
│                           │
├── read ───────┐           │
├── program ────┤           │
└── erase ──────┤           │
                │           │
                ▼           ▼
        w25q256_xxx()    w25q256
                           │
                           ▼
                        hqspi1
                           │
                           ▼
                         HAL
                           │
                           ▼
                       W25Q256
```

---

# 14. 上层如何使用

上层完全不需要知道：

```c
w25q256_t
```

也不需要调用：

```c
w25q256_read()
w25q256_program()
w25q256_erase()
```

只需要：

```c
flash.read(
    flash.context,
    address,
    buffer,
    size);
```

或者：

```c
flash.program(
    flash.context,
    address,
    buffer,
    size);
```

或者：

```c
flash.erase(
    flash.context,
    address,
    size);
```

---

# 15. 上层为什么不直接调用 W25Q256

如果上层直接：

```c
w25q256_erase(...);

w25q256_program(...);

w25q256_read(...);
```

那么依赖关系就是：

```text
Firmware Service
       │
       ▼
    W25Q256
       │
       ▼
      HAL
```

Firmware Service 与 W25Q256 强耦合。

以后换成：

```text
MX25L256
```

就需要修改 Firmware Service。

---

使用：

```c
block_device_t
```

之后：

```text
Firmware Service
       │
       ▼
 block_device_t
       ▲
       │
   ┌───┴────────┐
   │            │
   ▼            ▼
W25Q256      MX25L256
Adapter      Adapter
   │            │
   ▼            ▼
  HAL          HAL
```

Firmware Service 只依赖：

```c
block_device_t
```

而不是：

```c
w25q256_t
```

---

# 16. 一个 Service 的实际写法

例如 Firmware Install Service：

```c
firmware_status_t firmware_install(
    block_device_t *target,
    const uint8_t *image,
    uint32_t image_size)
{
    firmware_status_t status;

    status = target->erase(
        target->context,
        0,
        image_size);

    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    status = target->program(
        target->context,
        0,
        image,
        image_size);

    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    return FIRMWARE_STATUS_OK;
}
```

注意这里没有：

```c
w25q256_xxx()
```

也没有：

```c
HAL_QSPI_xxx()
```

Service 只知道：

```c
block_device_t
```

因此它不关心底层具体实现。

---

# 17. `.` 和 `->`

如果对象本身是结构体：

```c
block_device_t flash;
```

访问：

```c
flash.read
flash.context
```

使用：

```c
.
```

例如：

```c
flash.read(
    flash.context,
    address,
    buffer,
    size);
```

---

如果是结构体指针：

```c
block_device_t *flash;
```

则使用：

```c
->
```

例如：

```c
flash->read(
    flash->context,
    address,
    buffer,
    size);
```

其中：

```c
flash->read
```

等价于：

```c
(*flash).read
```

因此：

```c
flash->read(...)
```

本质就是：

```c
(*flash).read(...)
```

只是 `->` 更简洁。

---

# 18. `Provider` 和 `Caller`

接口设计中经常出现两个概念：

```text
Provider
Caller
```

## Provider

Provider 是接口的实现者。

例如：

```text
W25Q256 Driver
```

它负责提供：

```c
w25q256_read
w25q256_program
w25q256_erase
```

并持有：

```c
w25q256_t
```

所以：

> Provider 持有 Context。

---

## Caller

Caller 是接口使用者。

例如：

```text
Firmware Install Service
```

它调用：

```c
device->read(...)
device->program(...)
device->erase(...)
```

Caller 不需要知道：

```c
context
```

具体是什么类型。

---

# 19. Buffer 所有权

接口注释：

```text
Provider 持有 context。

调用者持有 data Buffer，
Buffer 只在 Callback 执行期间使用。
```

这实际上是在定义接口的：

> Ownership Contract。

例如：

```c
uint8_t buffer[256];

device->read(
    device->context,
    0,
    buffer,
    sizeof(buffer));
```

这里：

```c
buffer
```

属于 Caller。

Provider 可以在：

```c
read()
```

执行期间使用它。

但是 Provider 不应该：

```c
static void *saved_buffer;

saved_buffer = data;
```

然后在函数返回以后继续访问。

即：

```text
Caller
  │
  │ buffer
  ▼
read()
  │
  │ 可以使用
  ▼
return
  │
  └── 从这里开始 Provider 不应该再访问 buffer
```

这可以避免：

* 悬空指针
* 生命周期问题
* 异步访问错误
* 内存所有权不清晰

---

# 20. 为什么强调“同步接口”

注释中：

```text
同步 Block Device 接口
```

表示：

```c
device->read(...);
```

返回时：

> Read 操作已经完成。

同样：

```c
device->program(...);
```

返回时：

> Program 操作已经完成。

因此可以安全地规定：

```text
Buffer 只在 Callback 执行期间有效。
```

如果是异步接口：

```c
device->read_async(...);
```

那么函数返回后 DMA 可能还在访问：

```c
buffer
```

这时 Buffer 生命周期规则就完全不同。

所以同步/异步是接口 Contract 中非常重要的一部分。

---

# 21. 越界和对齐检查

注释：

```text
操作必须拒绝越界、未对齐或其他不支持的请求。
```

例如设备：

```text
容量：

32 MB

Program Alignment：

256 Byte

Erase Alignment：

4096 Byte
```

如果调用：

```c
erase(
    context,
    123,
    4096);
```

地址：

```text
123
```

不是 4096 对齐。

Provider 应返回错误。

例如：

```c
if ((address % erase_size) != 0)
{
    return FIRMWARE_STATUS_INVALID_ARGUMENT;
}
```

如果：

```text
address + size > device_size
```

也必须拒绝：

```c
if (address > device_size ||
    size > device_size - address)
{
    return FIRMWARE_STATUS_OUT_OF_RANGE;
}
```

这属于：

> 接口实现者必须遵守的 Contract。

---

# 22. 这种设计实际上解决了三个问题

整个：

```c
struct + function pointer + void *context
```

模式实际上同时解决三个问题。

## 第一：接口抽象

```c
block_device_t
```

规定：

```text
Block Device 能做什么
```

而不是：

```text
Block Device 怎么实现
```

---

## 第二：运行时绑定

例如：

```c
device.read = w25q256_read;
```

也可以：

```c
device.read = mx25_read;
```

因此：

```c
device.read(...)
```

最终调用哪个函数，是创建对象时决定的。

---

## 第三：实例状态

通过：

```c
void *context;
```

函数可以知道：

> 当前操作的是哪个设备实例。

因此可以同时存在：

```c
block_device_t flash1;
block_device_t flash2;
```

例如：

```c
flash1.context = &w25q256_1;
flash2.context = &w25q256_2;
```

即使：

```c
flash1.read == flash2.read
```

它们也可以操作不同硬件。

---

# 23. 一个非常重要的例子：两个相同设备

假设板子上有两个 W25Q256：

```c
static w25q256_t flash_a =
{
    .hqspi = &hqspi1,
};

static w25q256_t flash_b =
{
    .hqspi = &hqspi2,
};
```

创建：

```c
block_device_t device_a =
{
    .context = &flash_a,
    .read = w25q256_read,
};

block_device_t device_b =
{
    .context = &flash_b,
    .read = w25q256_read,
};
```

注意：

```c
device_a.read == device_b.read
```

两个对象使用同一个函数：

```c
w25q256_read
```

但是：

```c
device_a.context != device_b.context
```

因此：

```c
device_a.read(device_a.context, ...);
```

最终操作：

```text
QSPI1
```

而：

```c
device_b.read(device_b.context, ...);
```

最终操作：

```text
QSPI2
```

这正是 `context` 存在的重要意义。

---

# 24. 与全局变量方案的区别

不使用 Context 时，可能会：

```c
static QSPI_HandleTypeDef *g_hqspi;

firmware_status_t w25q256_read(...)
{
    HAL_QSPI_Receive(g_hqspi, ...);
}
```

这种设计的问题是：

```text
全局状态
    ↓
只能方便地绑定一个实例
    ↓
测试困难
    ↓
依赖隐藏
    ↓
模块耦合增加
```

使用：

```c
void *context
```

后：

```text
实例状态
    ↓
显式传递
    ↓
支持多个实例
    ↓
方便 Mock
    ↓
方便依赖注入
```

通常更适合大型嵌入式软件架构。

---

# 25. 与 C++ 的对应关系

可以近似理解：

| C                | C++                      |
| ---------------- | ------------------------ |
| `block_device_t` | interface/object         |
| `void *context`  | `this`                   |
| `read`           | virtual method           |
| `program`        | virtual method           |
| `erase`          | virtual method           |
| Adapter          | concrete implementation  |
| 函数指针赋值           | virtual function binding |

例如 C：

```c
device->read(
    device->context,
    address,
    data,
    size);
```

概念上类似 C++：

```cpp
device->read(
    address,
    data,
    size);
```

区别是 C 需要显式传：

```c
context
```

而 C++ 编译器会隐式传：

```cpp
this
```

---

# 26. 常见调用链

在嵌入式项目中可以形成：

```text
Application
     │
     ▼
Service
     │
     ▼
Interface
block_device_t
     │
     ▼
Adapter
w25q256_adapter
     │
     ▼
Device Driver
w25q256
     │
     ▼
BSP / HAL
     │
     ▼
Hardware
```

Service 不应该关心：

```text
QSPI
SPI
HAL
W25Q256
MX25
```

Service 只关心：

```text
Block Device
```

也就是：

```c
block_device_t
```

---

# 27. 常见代码模板

以后看到类似设计，可以直接套用下面的理解方式。

## 定义函数指针类型

```c
typedef return_type (*xxx_fn)(
    void *context,
    ...);
```

理解为：

> 定义一种叫 `xxx_fn` 的函数指针类型。

---

## 定义接口

```c
typedef struct
{
    void *context;

    xxx_fn operation;

} xxx_t;
```

理解为：

> 定义一个接口对象。

其中：

```text
context
```

保存实例。

```text
operation
```

保存操作函数。

---

## 创建实现

```c
xxx_t object =
{
    .context = &instance,
    .operation = concrete_operation,
};
```

理解为：

> 把具体实例和具体实现绑定到接口。

---

## 调用

```c
object.operation(
    object.context,
    ...);
```

理解为：

> 通过接口调用具体实现，并把当前实例传给实现函数。

---

# 28. 一句话总结

看到：

```c
typedef struct
{
    void *context;

    block_device_get_info_fn get_info;
    block_device_read_fn read;
    block_device_program_fn program;
    block_device_erase_fn erase;

} block_device_t;
```

可以直接理解成：

> `block_device_t` 是一个 C 语言实现的 Block Device 抽象接口对象。

其中：

```text
block_device_t
    │
    ├── context
    │      ↓
    │   “我是谁”
    │
    ├── get_info
    ├── read
    ├── program
    └── erase
           ↓
       “我能做什么”
```

最终形成：

```text
                 block_device_t
                       │
            ┌──────────┴──────────┐
            │                     │
         context              operations
            │                     │
            ▼                     ▼
      具体设备实例             函数指针
            │                     │
            └──────────┬──────────┘
                       ▼
                   具体实现
                       │
                       ▼
                    Hardware
```

因此这种模式的核心可以记成：

```text
struct
    +
function pointer
    +
void *context
    ↓
C 风格接口对象
    ↓
抽象
解耦
依赖注入
多实例
可测试
```

它是现代嵌入式 C 软件架构中非常常见、也非常实用的一种接口设计方式。
