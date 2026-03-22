/**
 * @file dma_backend.c
 * @brief GD32E23x DMA backend initialization (HAL).
 */
#include "gd32e23x_hal.h"

#include "platform/dma.h"

/**
 * @brief Initialize DMA clock and enable DMA related interrupts in NVIC.
 */
void platform_dma_init(void) {
    hal_rcu_periph_clk_enable(RCU_DMA);

    hal_nvic_periph_irq_enable(DMA_Channel0_IRQn, 0);
    hal_nvic_periph_irq_enable(DMA_Channel1_2_IRQn, 2);
    hal_nvic_periph_irq_enable(DMA_Channel3_4_IRQn, 2);
}
