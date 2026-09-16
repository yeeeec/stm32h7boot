#include "bsp/bsp_isp_uart.h"

#include "usart.h"

#include <stddef.h>

#define BSP_ISP_UART_BAUD_RATE 115200U

#define ISP_UART huart2

static firmware_status_t ConvertHalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_BUSY)
    {
        return FIRMWARE_STATUS_BUSY;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t ConfigureUart(uint32_t word_length, uint32_t parity)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Abort(&ISP_UART);
    if ((status != HAL_OK) && (status != HAL_ERROR))
    {
        return ConvertHalStatus(status);
    }

    status = HAL_UART_DeInit(&ISP_UART);
    if (status != HAL_OK)
    {
        return ConvertHalStatus(status);
    }

    ISP_UART.Instance                    = USART2;
    ISP_UART.Init.BaudRate               = BSP_ISP_UART_BAUD_RATE;
    ISP_UART.Init.WordLength             = word_length;
    ISP_UART.Init.StopBits               = UART_STOPBITS_1;
    ISP_UART.Init.Parity                 = parity;
    ISP_UART.Init.Mode                   = UART_MODE_TX_RX;
    ISP_UART.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    ISP_UART.Init.OverSampling           = UART_OVERSAMPLING_16;
    ISP_UART.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    ISP_UART.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    ISP_UART.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    status = HAL_UART_Init(&ISP_UART);
    if (status != HAL_OK)
    {
        return ConvertHalStatus(status);
    }
    status = HAL_UARTEx_SetTxFifoThreshold(&ISP_UART, UART_TXFIFO_THRESHOLD_1_8);
    if (status != HAL_OK)
    {
        return ConvertHalStatus(status);
    }
    status = HAL_UARTEx_SetRxFifoThreshold(&ISP_UART, UART_RXFIFO_THRESHOLD_1_8);
    if (status != HAL_OK)
    {
        return ConvertHalStatus(status);
    }
    status = HAL_UARTEx_DisableFifoMode(&ISP_UART);
    return ConvertHalStatus(status);
}

firmware_status_t BspIspUart_Configure(bsp_isp_uart_mode_t mode)
{
    if (mode == BSP_ISP_UART_APPLICATION_MODE)
    {
        return ConfigureUart(UART_WORDLENGTH_8B, UART_PARITY_NONE);
    }
    if (mode == BSP_ISP_UART_ROM_MODE)
    {
        /* With parity enabled, 9B word length produces 8 data bits + parity. */
        return ConfigureUart(UART_WORDLENGTH_9B, UART_PARITY_EVEN);
    }
    return FIRMWARE_STATUS_INVALID_ARGUMENT;
}

firmware_status_t BspIspUart_Transmit(const uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX) || (timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return ConvertHalStatus(HAL_UART_Transmit(&ISP_UART, data, (uint16_t) size, timeout_ms));
}

firmware_status_t BspIspUart_Receive(uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX) || (timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return ConvertHalStatus(HAL_UART_Receive(&ISP_UART, data, (uint16_t) size, timeout_ms));
}
