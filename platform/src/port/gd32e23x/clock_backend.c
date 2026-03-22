/**
 * @file clock_backend.c
 * @brief GD32E23x clock and systick backend implementation (HAL).
 */
#include "gd32e23x_hal.h"

#include "platform/clock.h"

/** System tick counter incremented in SysTick_Handler. */
static volatile uint32_t g_sys_tick = 0;

/**
 * @brief Configure system clocks and peripheral clocks.
 */
void platform_clock_init(void) {
    hal_rcu_clk_struct rcu_clk_parameter;
    hal_rcu_osci_struct rcu_osci_parameter;
    hal_rcu_periphclk_struct rcu_periphclk_parameter;

    hal_rcu_struct_init(HAL_RCU_CLK_STRUCT, &rcu_clk_parameter);
    hal_rcu_struct_init(HAL_RCU_OSCI_STRUCT, &rcu_osci_parameter);
    hal_rcu_struct_init(HAL_RCU_PERIPHCLK_STRUCT, &rcu_periphclk_parameter);
    rcu_osci_parameter.hxtal.need_configure  = ENABLE;
    rcu_osci_parameter.hxtal.state           = RCU_OSC_ON;
    rcu_osci_parameter.irc8m.need_configure  = ENABLE;
    rcu_osci_parameter.irc8m.state           = RCU_OSC_ON;
    rcu_osci_parameter.irc8m.adjust_value    = 0;
    rcu_osci_parameter.irc40k.need_configure = ENABLE;
    rcu_osci_parameter.irc40k.state          = RCU_OSC_ON;
    rcu_osci_parameter.pll.need_configure    = ENABLE;
    rcu_osci_parameter.pll.state             = RCU_OSC_ON;
    rcu_osci_parameter.pll.pll_source        = RCU_PLL_SRC_HXTAL;
    rcu_osci_parameter.pll.pll_mul           = RCU_PLL_MULT8;
    rcu_osci_parameter.pll.pre_div           = RCU_PLL_PREDIV1;
    if (HAL_ERR_NONE != hal_rcu_osci_config(&rcu_osci_parameter)) {
        while (1) {
            ;
        }
    }
    rcu_clk_parameter.clock_type =
        RCU_CLKTYPE_SYSCLK | RCU_CLKTYPE_AHBCLK | RCU_CLKTYPE_APB1CLK | RCU_CLKTYPE_APB2CLK;
    rcu_clk_parameter.sysclk_source   = RCU_SYSCLK_SRC_PLL;
    rcu_clk_parameter.ahbclk_divider  = RCU_SYSCLK_AHBDIV1;
    rcu_clk_parameter.apb1clk_divider = RCU_AHBCLK_APB1DIV1;
    rcu_clk_parameter.apb2clk_divider = RCU_AHBCLK_APB2DIV1;
    if (HAL_ERR_NONE != hal_rcu_clock_config(&rcu_clk_parameter, WS_WSCNT_2)) {
        while (1) {
            ;
        }
    }
    rcu_periphclk_parameter.periph_clock_type = RCU_PERIPH_CLKTYPE_ADC;
    rcu_periphclk_parameter.adc_clock_source  = RCU_ADCCK_APB2_DIV4;
    if (HAL_ERR_NONE != hal_rcu_periph_clock_config(&rcu_periphclk_parameter)) {
        while (1) {
            ;
        }
    }
    rcu_periphclk_parameter.periph_clock_type   = RCU_PERIPH_CLKTYPE_USART0;
    rcu_periphclk_parameter.usart0_clock_source = RCU_USART0_CLKSRC_APB2;
    if (HAL_ERR_NONE != hal_rcu_periph_clock_config(&rcu_periphclk_parameter)) {
        while (1) {
            ;
        }
    }
}

/**
 * @brief Initialize system services and base tick.
 */
void platform_system_init(void) {
    hal_fmc_prefetch_enable();
    hal_rcu_periph_clk_enable(RCU_PMU);
    hal_rcu_periph_clk_enable(RCU_CFGCMP);
    hal_nvic_periph_irq_enable(NonMaskableInt_IRQn, 0);

    hal_nvic_periph_irq_enable(HardFault_IRQn, 0);

    hal_nvic_periph_irq_enable(SVCall_IRQn, 0);

    hal_nvic_periph_irq_enable(PendSV_IRQn, 0);

    hal_nvic_periph_irq_enable(SysTick_IRQn, 0);

    hal_basetick_init(HAL_BASETICK_SOURCE_SYSTICK);
}

/**
 * @brief Get current system tick value.
 *
 * @return Tick counter value.
 */
uint32_t clock_get_systick(void) {
    return g_sys_tick;
}

/**
 * @brief Delay in milliseconds using system tick.
 *
 * @param ms Delay time in milliseconds.
 */
void clock_delay_ms(uint32_t ms) {
    uint32_t start = g_sys_tick;
    while ((g_sys_tick - start) < ms) {
        ;
    }
}

/**
 * @brief SysTick interrupt handler.
 */
void SysTick_Handler(void) {
    hal_basetick_irq();
    g_sys_tick++;
}
