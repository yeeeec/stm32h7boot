/**
 * @file bsp_dislay.c
 * @brief 外部显示屏的backlight线性绑定。
 *
 */
#include "bsp/bsp_display.h"

#include "tim.h"

// static firmware_status_t HalStatus(HAL_StatusTypeDef status)
// {
//     if (status == HAL_OK)
//     {
//         return FIRMWARE_STATUS_OK;
//     }
//     if (status == HAL_TIMEOUT)
//     {
//         return FIRMWARE_STATUS_TIMEOUT;
//     }
//     return FIRMWARE_STATUS_IO_ERROR;
// }

firmware_status_t BSP_BacklightInit(void)
{
    if (htim1.Instance != TIM1)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    htim1.Init.Period = 20000 - 1;
    HAL_TIM_Base_Init(&htim1);

    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }

    BSP_BacklightSetPermille(0);

    return FIRMWARE_STATUS_OK;
}

void BSP_BacklightSetPermille(uint16_t percent)
{
    if (percent > 1000U)
    {
        percent = 1000U;
    }

    uint32_t arr     = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t compare = ((arr + 1U) * percent) / 1000U;
    if (compare > arr)
    {
        compare = arr;
    }

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, compare);
}

void BSP_BacklightEnable(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}

void BSP_BacklightDisable(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}
