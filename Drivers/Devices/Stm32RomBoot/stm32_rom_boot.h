/**
 * @file stm32_rom_boot.h
 * @brief 通过 UART 访问 STM32 System Memory Bootloader 的硬件无关驱动。
 *
 * 本驱动实现 ST AN3155 定义的 UART bootloader 基础帧格式。驱动本身不
 * 包含 HAL，也不知道主控使用哪一个 USART、目标芯片的 BOOT0/NRST 接在
 * 哪些 GPIO 上；这些板级细节全部通过 stm32_rom_boot_port_t 注入。
 *
 * 驱动只提供单个协议事务的同步原语。长镜像的校验、分块调度、看门狗
 * 喂狗、掉电恢复和发布包策略应由上层 Service 负责。这样可以让 Service
 * 在每次 Process() 中只调用一个有限大小的 Read/Write/Erase 操作。
 */
#ifndef DEVICE_STM32_ROM_BOOT_H
#define DEVICE_STM32_ROM_BOOT_H

#include <stdint.h>

#include "firmware/status.h"

/** STM32 UART bootloader 的协议控制字节。 */
#define STM32_ROM_BOOT_SYNC_BYTE 0x7FU
#define STM32_ROM_BOOT_ACK_BYTE  0x79U
#define STM32_ROM_BOOT_NACK_BYTE 0x1FU

/** AN3155 中定义的常用命令。目标 ROM 不一定实现全部命令。 */
#define STM32_ROM_BOOT_COMMAND_GET            0x00U
#define STM32_ROM_BOOT_COMMAND_GET_VERSION    0x01U
#define STM32_ROM_BOOT_COMMAND_GET_ID         0x02U
#define STM32_ROM_BOOT_COMMAND_READ_MEMORY    0x11U
#define STM32_ROM_BOOT_COMMAND_GO             0x21U
#define STM32_ROM_BOOT_COMMAND_WRITE_MEMORY   0x31U
#define STM32_ROM_BOOT_COMMAND_ERASE          0x43U
#define STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE 0x44U

/** 协议一次 Read/Write Memory 事务允许的最大数据长度。 */
#define STM32_ROM_BOOT_MAX_MEMORY_TRANSFER 256U

/** Get 命令返回的最大支持命令数量，足够覆盖已知 STM32 ROM 版本。 */
#define STM32_ROM_BOOT_MAX_COMMAND_COUNT 32U

/** 擦除一条协议帧中允许携带的最大页数量。调用方可分多次擦除。 */
#define STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT 256U

/** Extended Erase 最大帧长度：命令数据、256 个 16-bit 页号和校验字节。 */
#define STM32_ROM_BOOT_MAX_ERASE_FRAME_SIZE (2U + (STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT * 2U) + 1U)

/** 选择标准 Erase Memory 或 Extended Erase 命令。 */
typedef enum
{
    /** 页号和页数均使用一个字节的旧协议。 */
    STM32_ROM_BOOT_ERASE_STANDARD = 0,
    /** 页号和页数使用两个字节，适用于较大的 Flash。 */
    STM32_ROM_BOOT_ERASE_EXTENDED
} stm32_rom_boot_erase_mode_t;

/** 主控 UART 在 ROM Bootloader 和目标 Application 之间的配置模式。 */
typedef enum
{
    /** UART 逻辑配置为 8 data bits、even parity、1 stop bit。 */
    STM32_ROM_BOOT_UART_ROM_MODE = 0,
    /** UART 恢复为目标 Application 所需的板级配置。 */
    STM32_ROM_BOOT_UART_APPLICATION_MODE
} stm32_rom_boot_uart_mode_t;

/**
 * @brief 切换主控 UART 的帧格式。
 *
 * STM32 System Memory UART 协议使用偶校验；目标 Application 往往使用
 * 不同的帧格式。回调可以重新配置同一个 HAL UART，也可以切换到另一
 * 个已初始化的 UART。若不需要由驱动切换配置，可以传入 NULL，并由
 * 调用方在 Enter 前后自行完成配置。
 */
typedef firmware_status_t (*stm32_rom_boot_configure_uart_fn)(void *context,
                                                              stm32_rom_boot_uart_mode_t mode);

/** 发送一段已经组帧的 UART 字节；函数返回前必须完成或报告传输结果。 */
typedef firmware_status_t (*stm32_rom_boot_transmit_fn)(void *context, const uint8_t *data,
                                                        uint32_t size, uint32_t timeout_ms);

/** 接收指定数量的 UART 字节；不能在超时后返回“部分成功”。 */
typedef firmware_status_t (*stm32_rom_boot_receive_fn)(void *context, uint8_t *data, uint32_t size,
                                                       uint32_t timeout_ms);

/** 控制目标 MCU 的 BOOT0 电平；非零表示拉高，零表示拉低。 */
typedef firmware_status_t (*stm32_rom_boot_set_boot0_fn)(void *context, int high);

/** 对目标 MCU 产生一次复位脉冲。 */
typedef firmware_status_t (*stm32_rom_boot_reset_target_fn)(void *context);

/** 在协议步骤之间提供毫秒级等待。该回调不得依赖目标 MCU 的响应。 */
typedef void (*stm32_rom_boot_delay_ms_fn)(void *context, uint32_t delay_ms);

/**
 * @brief STM32 ROM Bootloader 的板级传输端口。
 *
 * 所有函数都使用同一个 context。驱动不保存或释放 context 指向的对象；
 * 所有回调、UART 句柄和 GPIO 对象的生命周期必须覆盖整个驱动实例。
 */
typedef struct
{
    /** 原样传递给本结构中所有回调的板级上下文。 */
    void *context;
    /** 可选 UART 模式切换；为 NULL 时由调用方在驱动外配置 UART。 */
    stm32_rom_boot_configure_uart_fn configure_uart;
    /** 必需的同步 UART 发送回调。 */
    stm32_rom_boot_transmit_fn transmit;
    /** 必需的定长同步 UART 接收回调。 */
    stm32_rom_boot_receive_fn receive;
    /** 必需的目标 BOOT0 GPIO 控制回调。 */
    stm32_rom_boot_set_boot0_fn set_boot0;
    /** 必需的目标 NRST 复位序列回调。 */
    stm32_rom_boot_reset_target_fn reset_target;
    /** 必需的毫秒延时回调，用于等待目标复位后进入 ROM。 */
    stm32_rom_boot_delay_ms_fn delay_ms;
} stm32_rom_boot_port_t;

/**
 * @brief 驱动配置。
 *
 * expected_device_id 为 0xFFFF 时不做 ID 比较，仅适合实验室探测；量产
 * 构建应填写 AN3155 Get ID 返回的目标 ID，避免把固件写入错误的 MCU。
 */
typedef struct
{
    /** 普通命令、地址阶段和短数据收发的最大等待时间，单位毫秒。 */
    uint32_t command_timeout_ms;
    /** Write Memory 最终 ACK 的最大等待时间，单位毫秒。 */
    uint32_t write_timeout_ms;
    /** Erase/Extended Erase 最终 ACK 的最大等待时间，单位毫秒。 */
    uint32_t erase_timeout_ms;
    /** 释放目标复位后，到发送 0x7F 同步字节前的等待时间，单位毫秒。 */
    uint32_t reset_settle_ms;
    /** Get ID 期望值；0xFFFF 表示仅探测而不校验具体目标型号。 */
    uint16_t expected_device_id;
    /** 按目标 ROM 能力选择标准或扩展擦除帧。 */
    stm32_rom_boot_erase_mode_t erase_mode;
} stm32_rom_boot_config_t;

/** Get 命令返回的 ROM Bootloader 能力信息。 */
typedef struct
{
    /** Get 返回的 ROM Bootloader 版本字节。 */
    uint8_t bootloader_version;
    /** Get ID 返回的 16-bit STM32 Device ID。 */
    uint16_t device_id;
    /** commands 数组中的有效元素数量。 */
    uint8_t command_count;
    /** Get 返回的受支持命令码，未使用的尾部元素无业务含义。 */
    uint8_t commands[STM32_ROM_BOOT_MAX_COMMAND_COUNT];
} stm32_rom_boot_info_t;

/** 驱动对象必须由调用方静态分配并在 Init 前清零。 */
typedef struct stm32_rom_boot
{
    /** Init 时复制并冻结的板级端口回调。 */
    stm32_rom_boot_port_t port;
    /** Init 时复制并冻结的协议和目标配置。 */
    stm32_rom_boot_config_t config;
    /** 最近一次 GetInfo 成功获得的目标能力信息。 */
    stm32_rom_boot_info_t info;
    /** Init 成功后置位，防止同一对象重复初始化。 */
    int initialized;
    /** 0x7F 同步成功后置位，Leave 或进入失败后清零。 */
    int in_bootloader;
} stm32_rom_boot_t;

/**
 * @brief 初始化驱动对象。
 *
 * @param[out] device 调用方拥有的、尚未初始化的驱动对象。
 * @param[in] port 已完成板级绑定的 UART/GPIO 端口。
 * @param[in] config 目标设备 ID 和协议超时配置。
 *
 * @return FIRMWARE_STATUS_OK 初始化成功。
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT 参数、回调或超时无效。
 * @return FIRMWARE_STATUS_INVALID_STATE 驱动对象已经初始化。
 */
firmware_status_t Stm32RomBoot_Init(stm32_rom_boot_t *device, const stm32_rom_boot_port_t *port,
                                    const stm32_rom_boot_config_t *config);

/**
 * @brief 拉高 BOOT0、复位目标并完成 0x7F 同步握手。
 *
 * 成功返回后目标处于 System Memory Bootloader，后续可调用 Get/Read/Write
 * 等接口。失败时驱动会尽力把 BOOT0 拉低并复位目标；调用方仍应检查返回值。
 *
 * @return FIRMWARE_STATUS_OK 已收到目标 ROM 的 ACK。
 * @return FIRMWARE_STATUS_TIMEOUT 同步 ACK 未在超时时间内到达。
 * @return FIRMWARE_STATUS_IO_ERROR 收到 NACK、非法响应或 GPIO/UART 失败。
 */
firmware_status_t Stm32RomBoot_Enter(stm32_rom_boot_t *device);

/**
 * @brief 读取 ROM 版本、支持命令列表和 Device ID，并完成配置的 ID 校验。
 *
 * @return FIRMWARE_STATUS_OK 信息完整且 Device ID 符合配置。
 * @return FIRMWARE_STATUS_NOT_FOUND Device ID 与期望值不一致。
 * @return FIRMWARE_STATUS_NOT_SUPPORTED 目标没有 Get 或 Get ID 能力。
 * @return FIRMWARE_STATUS_IO_ERROR UART 收发失败或收到 NACK/非法响应。
 */
firmware_status_t Stm32RomBoot_GetInfo(stm32_rom_boot_t *device, stm32_rom_boot_info_t *info);

/**
 * @brief 判断目标 ROM 是否声明支持某个命令。
 *
 * @return 非零表示 GetInfo 最近一次成功返回了该命令，零表示不支持或
 *         尚未成功完成 GetInfo。该查询不会访问 UART。
 */
int Stm32RomBoot_IsCommandSupported(const stm32_rom_boot_t *device, uint8_t command);

/**
 * @brief 从目标 Flash 读取一个协议允许大小的字节块。
 *
 * @param[in] address 目标 MCU 的绝对地址，不是 H743 的 QSPI 偏移。
 * @param[out] data 接收缓冲区。
 * @param[in] size 取值范围为 1..256 字节。
 *
 * @return FIRMWARE_STATUS_OK 已收到完整数据块。
 * @return FIRMWARE_STATUS_NOT_SUPPORTED 目标没有 Read Memory 命令。
 * @return FIRMWARE_STATUS_TIMEOUT UART 等待响应超时。
 * @return FIRMWARE_STATUS_IO_ERROR 收到 NACK、非法 ACK 或传输失败。
 */
firmware_status_t Stm32RomBoot_ReadMemory(stm32_rom_boot_t *device, uint32_t address, uint8_t *data,
                                          uint32_t size);

/**
 * @brief 向目标 Flash 写入一个协议允许大小的字节块。
 *
 * 目标地址、Flash 对齐和目标可写范围由上层产品配置及 ROM Bootloader
 * 约束负责；驱动只检查 AN3155 帧长度。调用方必须在写入前完成擦除。
 *
 * @return FIRMWARE_STATUS_OK 目标已接受并完成该写入事务。
 * @return FIRMWARE_STATUS_NOT_SUPPORTED 目标没有 Write Memory 命令。
 * @return FIRMWARE_STATUS_TIMEOUT 写入完成 ACK 超时。
 * @return FIRMWARE_STATUS_IO_ERROR 收到 NACK、非法 ACK 或传输失败。
 */
firmware_status_t Stm32RomBoot_WriteMemory(stm32_rom_boot_t *device, uint32_t address,
                                           const uint8_t *data, uint32_t size);

/**
 * @brief 擦除一组连续页。
 *
 * page_start 和 page_count 是目标 ROM 协议定义的页号，不是字节地址。
 * 单次最多携带 STM32_ROM_BOOT_MAX_ERASE_PAGE_COUNT 页；更大的镜像应由
 * 上层分成多个调用。扩展擦除模式使用 16-bit 页号，标准模式要求页号
 * 和页数都能放入 8-bit。
 *
 * @return FIRMWARE_STATUS_OK 目标已完成这组页的擦除。
 * @return FIRMWARE_STATUS_NOT_SUPPORTED 目标不支持选择的擦除命令，或标准
 *         模式无法表达请求的页范围。
 * @return FIRMWARE_STATUS_TIMEOUT 擦除完成 ACK 超时。
 * @return FIRMWARE_STATUS_IO_ERROR 收到 NACK、非法 ACK 或传输失败。
 */
firmware_status_t Stm32RomBoot_ErasePages(stm32_rom_boot_t *device, uint16_t page_start,
                                          uint16_t page_count);

/**
 * @brief 退出 ROM Bootloader 并复位目标进入用户 Application。
 *
 * 驱动会先拉低 BOOT0，再恢复 Application UART 配置，最后复位目标。退出
 * 后不能继续调用 Read/Write/Erase，必须重新 Enter。
 *
 * @return FIRMWARE_STATUS_OK 目标已切换到用户启动条件。
 * @return FIRMWARE_STATUS_IO_ERROR BOOT0、UART 配置或目标复位回调失败。
 */
firmware_status_t Stm32RomBoot_Leave(stm32_rom_boot_t *device);

#endif
