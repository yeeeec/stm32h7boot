#include "bsp/bsp_therapy_gpio.h"

#include "gpio.h"

#define THERAPY_BOOT0_GPIO_PORT GPIOG
#define THERAPY_BOOT0_PIN       GPIO_PIN_2
#define THERAPY_RESET_GPIO_PORT GPIOB
#define THERAPY_RESET_PIN       GPIO_PIN_0

static int s_initialized;

firmware_status_t BspTherapyGpio_Init(void)
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    init.Mode  = GPIO_MODE_OUTPUT_PP;
    init.Pull  = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin   = THERAPY_BOOT0_PIN;
    HAL_GPIO_Init(THERAPY_BOOT0_GPIO_PORT, &init);
    init.Pin = THERAPY_RESET_PIN;
    HAL_GPIO_Init(THERAPY_RESET_GPIO_PORT, &init);
    HAL_GPIO_WritePin(THERAPY_BOOT0_GPIO_PORT, THERAPY_BOOT0_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(THERAPY_RESET_GPIO_PORT, THERAPY_RESET_PIN, GPIO_PIN_SET);
    s_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BspTherapy_SetBoot0(int high)
{
    if (s_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    HAL_GPIO_WritePin(THERAPY_BOOT0_GPIO_PORT, THERAPY_BOOT0_PIN,
                      (high != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BspTherapy_SetReset(int asserted)
{
    if (s_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    HAL_GPIO_WritePin(THERAPY_RESET_GPIO_PORT, THERAPY_RESET_PIN,
                      (asserted != 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return FIRMWARE_STATUS_OK;
}
