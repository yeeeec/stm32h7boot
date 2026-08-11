/**
 * @file bsp_stm32_rom_boot.c
 * @brief STM32 从 MCU ROM Bootloader 的 USART2/GPIO 板级实现。
 *
 * 硬件连接：
 * - PA2  -> 从 MCU UART RX（USART2_TX）；
 * - PA3  <- 从 MCU UART TX（USART2_RX）；
 * - PC7  -> 从 MCU BOOT，高电平选择 System Memory；
 * - PG14 -> 从 MCU RST，高电平保持复位。
 *
 * ST ROM UART 协议在线路上使用 8 data bits、even parity、1 stop bit。STM32 HAL
 * 的 WordLength 包含校验位，因此该格式必须写成 UART_WORDLENGTH_9B 与
 * UART_PARITY_EVEN，不能配置为 HAL 的 8B + EVEN（后者只有 7 个有效数据位）。
 */
#include "bsp/bsp_stm32_rom_boot.h"

#include <stddef.h>

#include "main.h"
#include "stm32_rom_boot.h"
#include "usart.h"

/** USART2 在 ROM 和从 MCU 应用模式下统一使用的波特率。 */
#define BSP_STM32_ROM_BOOT_BAUD_RATE 115200U
/** RST 高电平保持时间；远大于 STM32 NRST 的最小脉宽要求。 */
#define BSP_STM32_ROM_BOOT_RESET_PULSE_MS 5U

/* 以下超时属于板级通信预算，上层升级服务仍应限制整次升级的总时长。 */
#define BSP_STM32_ROM_BOOT_COMMAND_TIMEOUT_MS 50U
#define BSP_STM32_ROM_BOOT_WRITE_TIMEOUT_MS 500U
#define BSP_STM32_ROM_BOOT_ERASE_TIMEOUT_MS 1000U
#define BSP_STM32_ROM_BOOT_RESET_SETTLE_MS 50U

/** 驱动层把 0xFFFF 定义为“不校验型号”；板级量产绑定明确禁止该值。 */
#define BSP_STM32_ROM_BOOT_UNSPECIFIED_DEVICE_ID 0xFFFFU

static stm32_rom_boot_t stm32_rom_boot;

/** 将 HAL 的四态结果收敛为跨平台 Firmware Status。 */
static firmware_status_t HalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

/**
 * @brief 按 ROM 8E1 或应用 8N1 重新初始化 USART2。
 *
 * 使用同步 Abort/DeInit/Init，确保上一次超时遗留的接收状态、错误标志和 FIFO
 * 内容不会污染下一种帧格式。这里直接调用 HAL 并返回错误，不调用 CubeMX 生成的
 * MX_USART2_UART_Init()，因为后者遇到错误会进入 Error_Handler()，无法向升级状态机
 * 报告可诊断的失败状态。
 */
static firmware_status_t ConfigureUart(
    void *context,
    stm32_rom_boot_uart_mode_t mode)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)context;
    HAL_StatusTypeDef hal_status;

    if ((uart == NULL) || (uart != &huart2) || (uart->Instance != USART2))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((mode != STM32_ROM_BOOT_UART_ROM_MODE) &&
        (mode != STM32_ROM_BOOT_UART_APPLICATION_MODE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    hal_status = HAL_UART_Abort(uart);
    if (hal_status != HAL_OK)
    {
        return HalStatus(hal_status);
    }
    hal_status = HAL_UART_DeInit(uart);
    if (hal_status != HAL_OK)
    {
        return HalStatus(hal_status);
    }

    uart->Instance = USART2;
    uart->Init.BaudRate = BSP_STM32_ROM_BOOT_BAUD_RATE;
    uart->Init.WordLength = (mode == STM32_ROM_BOOT_UART_ROM_MODE)
                                ? UART_WORDLENGTH_9B
                                : UART_WORDLENGTH_8B;
    uart->Init.StopBits = UART_STOPBITS_1;
    uart->Init.Parity = (mode == STM32_ROM_BOOT_UART_ROM_MODE)
                            ? UART_PARITY_EVEN
                            : UART_PARITY_NONE;
    uart->Init.Mode = UART_MODE_TX_RX;
    uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart->Init.OverSampling = UART_OVERSAMPLING_16;
    uart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    uart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    hal_status = HAL_UART_Init(uart);
    if (hal_status != HAL_OK)
    {
        return HalStatus(hal_status);
    }
    hal_status = HAL_UARTEx_SetTxFifoThreshold(
        uart, UART_TXFIFO_THRESHOLD_1_8);
    if (hal_status != HAL_OK)
    {
        return HalStatus(hal_status);
    }
    hal_status = HAL_UARTEx_SetRxFifoThreshold(
        uart, UART_RXFIFO_THRESHOLD_1_8);
    if (hal_status != HAL_OK)
    {
        return HalStatus(hal_status);
    }
    return HalStatus(HAL_UARTEx_DisableFifoMode(uart));
}

/** 阻塞发送完整协议帧；ROM 驱动已把长操作拆成有界大小的事务。 */
static firmware_status_t UartTransmit(
    void *context,
    const uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    if ((context != &huart2) || (data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT16_MAX)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return HalStatus(HAL_UART_Transmit(
        (UART_HandleTypeDef *)context,
        (uint8_t *)data,
        (uint16_t)size,
        timeout_ms));
}

/** 阻塞接收指定字节数；HAL 超时不能被当成“部分成功”。 */
static firmware_status_t UartReceive(
    void *context,
    uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    if ((context != &huart2) || (data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT16_MAX)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return HalStatus(HAL_UART_Receive(
        (UART_HandleTypeDef *)context,
        data,
        (uint16_t)size,
        timeout_ms));
}

/** PC7 高电平进入 System Memory，低电平选择用户 Flash。 */
static firmware_status_t SetBoot0(void *context, int high)
{
    (void)context;
    HAL_GPIO_WritePin(
        SECONDARY_MCU_BOOT_GPIO_Port,
        SECONDARY_MCU_BOOT_Pin,
        (high != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return FIRMWARE_STATUS_OK;
}

/** PG14 为高有效复位：拉高、保持固定时间，再拉低释放从 MCU。 */
static firmware_status_t ResetTarget(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(
        SECONDARY_MCU_RST_GPIO_Port,
        SECONDARY_MCU_RST_Pin,
        GPIO_PIN_RESET);
    HAL_Delay(BSP_STM32_ROM_BOOT_RESET_PULSE_MS);
    HAL_GPIO_WritePin(
        SECONDARY_MCU_RST_GPIO_Port,
        SECONDARY_MCU_RST_Pin,
        GPIO_PIN_SET);
    return FIRMWARE_STATUS_OK;
}

/** 驱动协议步骤所需的毫秒延时，时间基准来自主控 HAL Tick。 */
static void DelayMs(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

firmware_status_t BSP_Stm32RomBootInit(
    const bsp_stm32_rom_boot_config_t *config)
{
    stm32_rom_boot_port_t port;
    stm32_rom_boot_config_t driver_config;
    firmware_status_t status;

    if ((config == NULL) ||
        (config->expected_device_id ==
         BSP_STM32_ROM_BOOT_UNSPECIFIED_DEVICE_ID) ||
        ((config->erase_mode != BSP_STM32_ROM_BOOT_ERASE_STANDARD) &&
         (config->erase_mode != BSP_STM32_ROM_BOOT_ERASE_EXTENDED)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((stm32_rom_boot.initialized != 0) ||
        (huart2.Instance != USART2) ||
        (huart2.gState == HAL_UART_STATE_RESET))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 初始化/异常复位后的安全电平：正常启动，且不保持从 MCU 复位。 */
    HAL_GPIO_WritePin(
        SECONDARY_MCU_BOOT_GPIO_Port,
        SECONDARY_MCU_BOOT_Pin,
        GPIO_PIN_RESET);
    HAL_GPIO_WritePin(
        SECONDARY_MCU_RST_GPIO_Port,
        SECONDARY_MCU_RST_Pin,
        GPIO_PIN_SET);

    /* 接管 USART2 时先建立确定的应用通信格式，清除此前可能遗留的状态。 */
    status = ConfigureUart(&huart2, STM32_ROM_BOOT_UART_APPLICATION_MODE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    port.context = &huart2;
    port.configure_uart = ConfigureUart;
    port.transmit = UartTransmit;
    port.receive = UartReceive;
    port.set_boot0 = SetBoot0;
    port.reset_target = ResetTarget;
    port.delay_ms = DelayMs;

    driver_config.command_timeout_ms =
        BSP_STM32_ROM_BOOT_COMMAND_TIMEOUT_MS;
    driver_config.write_timeout_ms = BSP_STM32_ROM_BOOT_WRITE_TIMEOUT_MS;
    driver_config.erase_timeout_ms = BSP_STM32_ROM_BOOT_ERASE_TIMEOUT_MS;
    driver_config.reset_settle_ms = BSP_STM32_ROM_BOOT_RESET_SETTLE_MS;
    driver_config.expected_device_id = config->expected_device_id;
    driver_config.erase_mode =
        (config->erase_mode == BSP_STM32_ROM_BOOT_ERASE_STANDARD)
            ? STM32_ROM_BOOT_ERASE_STANDARD
            : STM32_ROM_BOOT_ERASE_EXTENDED;

    return Stm32RomBoot_Init(&stm32_rom_boot, &port, &driver_config);
}

struct stm32_rom_boot *BSP_Stm32RomBootDevice(void)
{
    return (stm32_rom_boot.initialized != 0) ? &stm32_rom_boot : NULL;
}
