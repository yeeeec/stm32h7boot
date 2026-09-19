#include "bsp/bsp_therapy_uart.h"

#include <stddef.h>

#include "usart.h"

#define THERAPY_UART_BAUD_RATE 115200U
#define THERAPY_UART_HANDLE    huart2

static firmware_status_t ConvertHalStatus(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK: return FIRMWARE_STATUS_OK;
        case HAL_BUSY: return FIRMWARE_STATUS_BUSY;
        case HAL_TIMEOUT: return FIRMWARE_STATUS_TIMEOUT;
        default: return FIRMWARE_STATUS_IO_ERROR;
    }
}

static firmware_status_t Configure(uint32_t word_length, uint32_t parity)
{
    HAL_StatusTypeDef status = HAL_UART_Abort(&THERAPY_UART_HANDLE);
    if ((status != HAL_OK) && (status != HAL_ERROR))
    {
        return ConvertHalStatus(status);
    }
    status = HAL_UART_DeInit(&THERAPY_UART_HANDLE);
    if (status != HAL_OK)
    {
        return ConvertHalStatus(status);
    }
    THERAPY_UART_HANDLE.Instance = USART2;
    THERAPY_UART_HANDLE.Init.BaudRate = THERAPY_UART_BAUD_RATE;
    THERAPY_UART_HANDLE.Init.WordLength = word_length;
    THERAPY_UART_HANDLE.Init.StopBits = UART_STOPBITS_1;
    THERAPY_UART_HANDLE.Init.Parity = parity;
    THERAPY_UART_HANDLE.Init.Mode = UART_MODE_TX_RX;
    THERAPY_UART_HANDLE.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    THERAPY_UART_HANDLE.Init.OverSampling = UART_OVERSAMPLING_16;
    THERAPY_UART_HANDLE.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    THERAPY_UART_HANDLE.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    THERAPY_UART_HANDLE.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    status = HAL_UART_Init(&THERAPY_UART_HANDLE);
    if (status == HAL_OK)
    {
        status = HAL_UARTEx_SetTxFifoThreshold(&THERAPY_UART_HANDLE, UART_TXFIFO_THRESHOLD_1_8);
    }
    if (status == HAL_OK)
    {
        status = HAL_UARTEx_SetRxFifoThreshold(&THERAPY_UART_HANDLE, UART_RXFIFO_THRESHOLD_1_8);
    }
    if (status == HAL_OK)
    {
        status = HAL_UARTEx_DisableFifoMode(&THERAPY_UART_HANDLE);
    }
    return ConvertHalStatus(status);
}

firmware_status_t BspTherapyUart_Configure(bsp_therapy_uart_mode_t mode)
{
    if (mode == BSP_THERAPY_UART_APPLICATION_MODE)
    {
        return Configure(UART_WORDLENGTH_8B, UART_PARITY_NONE);
    }
    if (mode == BSP_THERAPY_UART_ROM_MODE)
    {
        return Configure(UART_WORDLENGTH_9B, UART_PARITY_EVEN);
    }
    return FIRMWARE_STATUS_INVALID_ARGUMENT;
}

firmware_status_t BspTherapyUart_Transmit(const uint8_t *data, uint32_t size,
                                          uint32_t timeout_ms)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX) || (timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return ConvertHalStatus(HAL_UART_Transmit(&THERAPY_UART_HANDLE, (uint8_t *) data,
                                              (uint16_t) size, timeout_ms));
}

firmware_status_t BspTherapyUart_Receive(uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX) || (timeout_ms == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return ConvertHalStatus(HAL_UART_Receive(&THERAPY_UART_HANDLE, data, (uint16_t) size,
                                             timeout_ms));
}
