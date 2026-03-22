/**
 * @file irq_handlers.c
 * @brief Cortex-M exception handlers (stub implementations).
 */
#include "gd32_mappings.h"
#include "platform/timer.h"

/**
 * @brief Handle NMI exception.
 */
void NMI_Handler(void) {
}

/**
 * @brief Handle HardFault exception.
 */
void HardFault_Handler(void) {
    while (1) {
        ;
    }
}

/**
 * @brief Handle SVCall exception.
 */
void SVC_Handler(void) {
}

/**
 * @brief Handle PendSV exception.
 */
void PendSV_Handler(void) {
}

/**
 * @brief Handle SysTick exception.
 *
 * @note If SysTick_Handler is already defined elsewhere, do not duplicate it
 *       to avoid multiple definition errors.
 */
void SysTick_Handler(void) {
}
