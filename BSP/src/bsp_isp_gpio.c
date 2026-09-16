#include "bsp/bsp_isp_gpio.h"

#include "gpio.h"

#define BSP_ISP_RESET_LOW_TIME_MS 10U

#define ISP_BOOT0_GPIO_Port GPIOI
#define ISP_BOOT0_Pin       GPIO_PIN_8
#define ISP_BOOT0_CLOCK     __HAL_RCC_GPIOI_CLK_ENABLE
#define ISP_NRST_GPIO_Port  GPIOC
#define ISP_NRST_Pin        GPIO_PIN_13
#define ISP_NRST_CLOCK      __HAL_RCC_GPIOC_CLK_ENABLE

static int s_initialized;

void BspIspGpio_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    ISP_BOOT0_CLOCK();
    ISP_NRST_CLOCK();

    GPIO_InitStruct.Pin   = ISP_BOOT0_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ISP_BOOT0_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = ISP_NRST_Pin;
    HAL_GPIO_Init(ISP_NRST_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(ISP_BOOT0_GPIO_Port, ISP_BOOT0_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ISP_NRST_GPIO_Port, ISP_NRST_Pin, GPIO_PIN_SET);
    s_initialized = 1;
}

firmware_status_t BspIspGpio_SetBoot0(int high)
{
    if (s_initialized == 0)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    HAL_GPIO_WritePin(ISP_BOOT0_GPIO_Port, ISP_BOOT0_Pin,
                      (high != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BspIspGpio_ResetTarget(void)
{
    if (s_initialized == 0)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    HAL_GPIO_WritePin(ISP_NRST_GPIO_Port, ISP_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(BSP_ISP_RESET_LOW_TIME_MS);
    HAL_GPIO_WritePin(ISP_NRST_GPIO_Port, ISP_NRST_Pin, GPIO_PIN_SET);

    return FIRMWARE_STATUS_OK;
}
