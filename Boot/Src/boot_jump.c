#include "boot_jump.h"

#include "main.h"

static void Boot_Jump_Start(uint32_t stack_pointer __attribute__((unused)),
                            uint32_t reset_handler __attribute__((unused)))
  __attribute__((naked, noreturn));

static void Boot_Jump_Start(uint32_t stack_pointer __attribute__((unused)),
                            uint32_t reset_handler __attribute__((unused)))
{
  __asm volatile (
    "msr msp, r0\n"
    "movs r0, #0\n"
    "msr psp, r0\n"
    "msr basepri, r0\n"
    "msr faultmask, r0\n"
    "msr control, r0\n"
    "isb\n"
    "cpsie i\n"
    "bx r1\n");
}
static void Boot_Jump_DisableInterrupts(void)
{
  __disable_irq();

  for (uint32_t index = 0U; index < 8U; ++index)
  {
    NVIC->ICER[index] = 0xFFFFFFFFUL;
    NVIC->ICPR[index] = 0xFFFFFFFFUL;
  }
}

static void Boot_Jump_DisableCaches(void)
{
  SCB_DisableDCache();
  SCB_DisableICache();
}

BootError Boot_Jump_ToAddress(uint32_t vector_table_address)
{
  uint32_t stack_pointer;
  uint32_t reset_handler;

  stack_pointer = *(const uint32_t *)vector_table_address;
  reset_handler = *(const uint32_t *)(vector_table_address + sizeof(uint32_t));

  Boot_Jump_DisableInterrupts();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  Boot_Jump_DisableCaches();

  __DSB();
  __ISB();

  SCB->VTOR = vector_table_address;
  __DSB();
  __ISB();
  Boot_Jump_Start(stack_pointer, reset_handler);
}
