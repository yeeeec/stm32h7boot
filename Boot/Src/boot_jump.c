#include "boot_jump.h"

#include "main.h"
#include "boot_image.h"

typedef void (*BootEntryPoint)(void);

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

BootError Boot_Jump_ToSlot(BootSlot slot)
{
  uint32_t vector_table_address;
  uint32_t stack_pointer;
  uint32_t reset_handler;
  BootError error;

  error = Boot_Image_GetVectorTableAddress(slot, &vector_table_address);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  stack_pointer = *(const uint32_t *)vector_table_address;
  reset_handler = *(const uint32_t *)(vector_table_address + sizeof(uint32_t));

  Boot_Jump_DisableInterrupts();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;

  HAL_RCC_DeInit();
  HAL_DeInit();
  Boot_Jump_DisableCaches();

  __DSB();
  __ISB();

  SCB->VTOR = vector_table_address;
  __set_MSP(stack_pointer);
  __set_PSP(0U);
  __set_CONTROL(0U);
  __DSB();
  __ISB();
  ((BootEntryPoint)reset_handler)();

  return BOOT_ERR_JUMP_FAILED;
}
