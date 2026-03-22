/**
 * @file timer_backend.c
 * @brief GD32E23x basic timer backend implementation (TIMER5).
 */
#include "gd32e23x.h"

#include "platform/timer.h"

/** Upper-layer callback invoked on TIMER5 update. */
static Plat_Timer_Callback_t g_timer_callback = NULL;

/**
 * @brief Initialize TIMER5 as a periodic base timer.
 *
 * @param period_ms Period in milliseconds.
 * @return Platform status code.
 */
Plat_Status_t platform_timer_base_init(uint32_t period_ms) {
    timer_parameter_struct timer_initpara;

    rcu_periph_clock_enable(RCU_TIMER5);

    timer_deinit(TIMER5);

    uint16_t prescaler = (SystemCoreClock / 1000000U) - 1;
    uint32_t period    = (period_ms * 1000U) - 1;

    if (period > 0xFFFF) {
        return PLAT_ERR_INVALID_PARAM;
    }

    timer_initpara.prescaler         = prescaler;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = period;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;

    timer_init(TIMER5, &timer_initpara);

    timer_interrupt_enable(TIMER5, TIMER_INT_UP);

    nvic_irq_enable(TIMER5_IRQn, 2);

    timer_auto_reload_shadow_enable(TIMER5);

    return PLAT_OK;
}

/**
 * @brief Set TIMER5 periodic callback.
 *
 * @param cb Callback function pointer.
 */
void platform_timer_set_callback(Plat_Timer_Callback_t cb) {
    g_timer_callback = cb;
}

/**
 * @brief Start TIMER5 counter.
 */
void platform_timer_start(void) {
    timer_enable(TIMER5);
}

/**
 * @brief Stop TIMER5 counter.
 */
void platform_timer_stop(void) {
    timer_disable(TIMER5);
}

/**
 * @brief TIMER5 interrupt handler.
 */
void TIMER5_IRQHandler(void) {
    if (timer_interrupt_flag_get(TIMER5, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);

        if (g_timer_callback) {
            g_timer_callback();
        }
    }
}
