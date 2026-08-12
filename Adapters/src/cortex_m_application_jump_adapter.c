/**
 * @file cortex_m_application_jump_adapter.c
 * @brief 实现 Cortex-M7 的中断、向量表、栈和分支交接流程。
 */
#include "adapters/cortex_m_application_jump_adapter.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx.h"

typedef void (*application_entry_t)(void);

/**
 * @brief 清理 Bootloader 运行环境并跳转到应用复位入口。
 *
 * 跳转前关闭并清空 SysTick，屏蔽和清除 NVIC 中断，将 VTOR 指向应用向量表，
 * 最后加载应用 MSP 并调用 Reset Handler。正常情况下该函数不会返回。
 */
static firmware_status_t Execute(void *context, uint32_t vector_table_address)
{
    uint32_t initial_msp;
    uint32_t reset_handler;
    uint32_t index;
    application_entry_t entry;

    if ((context == NULL) || ((vector_table_address & 0x7FU) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    /* Cortex-M 向量表前两个 word 分别是初始 MSP 和 Reset Handler 地址。 */
    initial_msp   = *(const volatile uint32_t *) (uintptr_t) vector_table_address;
    reset_handler = *(const volatile uint32_t *) (uintptr_t) (vector_table_address + 4U);
    entry         = (application_entry_t) (uintptr_t) reset_handler;

    /* 关闭 Bootloader 的系统节拍和中断，避免状态泄漏到应用。 */
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;
    for (index = 0U; index < 8U; ++index)
    {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }
    /* 完成向量表和栈切换后再开放中断并进入应用。 */
    SCB->VTOR = vector_table_address;
    __DSB();
    __ISB();
    __set_MSP(initial_msp);
    __enable_irq();
    entry();

    return FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t CortexMApplicationJumpAdapter_Init(cortex_m_application_jump_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 适配器无额外硬件状态，回调上下文使用自身地址。 */
    adapter->interface.context = adapter;
    adapter->interface.execute = Execute;
    return FIRMWARE_STATUS_OK;
}

const application_jump_t *
CortexMApplicationJumpAdapter_Interface(const cortex_m_application_jump_adapter_t *adapter)
{
    /* 接口内嵌在适配器中，返回其稳定地址。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
