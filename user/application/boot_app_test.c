#include "boot_app.h"

#include <stdbool.h>

#include "boot_config.h"
#include "boot_extflash.h"
#include "boot_simple_flash.h"
#include "stm32h7xx_hal.h"

typedef void (*BootAppTestEntryPoint)(void);

static uint8_t g_boot_test_started;
volatile uint32_t g_boot_test_stage;
volatile uint32_t g_boot_test_app_base;
volatile uint32_t g_boot_test_app_sp;
volatile uint32_t g_boot_test_app_reset;
volatile uint32_t g_boot_test_app_nmi;
volatile uint32_t g_boot_test_app_hardfault;
volatile uint32_t g_boot_test_vector_valid;
volatile uint32_t g_boot_test_vector_error;

static bool Boot_App_Test_IsStackInRange(uint32_t value, uint32_t base, uint32_t size) {
    uint32_t end_address = base + size;
    return ((value & 0x7U) == 0U) && (value >= base) && (value <= end_address);
}

static bool Boot_App_Test_IsValidStack(uint32_t stack_pointer) {
    return Boot_App_Test_IsStackInRange(stack_pointer, BOOT_DTCM_BASE, BOOT_DTCM_SIZE) ||
           Boot_App_Test_IsStackInRange(stack_pointer, BOOT_AXI_SRAM_BASE, BOOT_AXI_SRAM_SIZE) ||
           Boot_App_Test_IsStackInRange(stack_pointer, BOOT_SRAM_D2_BASE, BOOT_SRAM_D2_SIZE) ||
           Boot_App_Test_IsStackInRange(stack_pointer, BOOT_SRAM_D3_BASE, BOOT_SRAM_D3_SIZE);
}

static bool Boot_App_Test_IsValidCodeAddress(uint32_t address) {
    uint32_t aligned_address;

    if ((address & 0x1U) == 0U) {
        return false;
    }

    aligned_address = address & ~1UL;
    return (aligned_address >= BOOT_DEFAULT_APP_BASE) &&
           (aligned_address < (BOOT_DEFAULT_APP_BASE + BOOT_DEFAULT_APP_SIZE));
}

static bool Boot_App_Test_ValidateVector(void) {
    if (Boot_App_Test_IsValidStack(g_boot_test_app_sp) == false) {
        g_boot_test_vector_error = 1U;
        return false;
    }

    if (Boot_App_Test_IsValidCodeAddress(g_boot_test_app_reset) == false) {
        g_boot_test_vector_error = 2U;
        return false;
    }

    g_boot_test_vector_error = 0U;
    return true;
}

static void Boot_App_Test_DisableInterrupts(void) {
    __disable_irq();

    for (uint32_t index = 0U; index < 8U; ++index) {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;
    SCB->ICSR     = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    SCB->CFSR     = SCB->CFSR;
    SCB->HFSR     = SCB->HFSR;
    SCB->DFSR     = SCB->DFSR;

    __DSB();
    __ISB();
}

static BootError Boot_App_Test_ReadAppVector(uint32_t app_base,
                                             uint32_t *stack_pointer,
                                             uint32_t *reset_handler) {
    BootError error;

    if ((stack_pointer == NULL) || (reset_handler == NULL)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_ExtFlash_IsRangeValid(app_base, sizeof(uint32_t) * 2U) != false) {
        error = Boot_ExtFlash_Init();
        if (error != BOOT_ERR_NONE) {
            return error;
        }
    }

    error = Boot_SimpleFlash_Read(app_base, stack_pointer, sizeof(*stack_pointer));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_SimpleFlash_Read(app_base + sizeof(*stack_pointer), reset_handler,
                                  sizeof(*reset_handler));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    g_boot_test_app_base      = app_base;
    g_boot_test_app_sp        = *stack_pointer;
    g_boot_test_app_reset     = *reset_handler;
    g_boot_test_app_nmi       = *(const volatile uint32_t *)(uintptr_t)(app_base + 8U);
    g_boot_test_app_hardfault = *(const volatile uint32_t *)(uintptr_t)(app_base + 12U);
    g_boot_test_vector_valid  = Boot_App_Test_ValidateVector() ? 1U : 0U;
    g_boot_test_stage         = (g_boot_test_vector_valid != 0U) ? 1U : 3U;

    return BOOT_ERR_NONE;
}

static void Boot_App_Test_JumpToApp(uint32_t app_base,
                                    uint32_t stack_pointer,
                                    uint32_t reset_handler) {
    g_boot_test_stage = 2U;
    Boot_App_Test_DisableInterrupts();

    SCB->VTOR = app_base;
    __set_MSP(stack_pointer);
    __set_CONTROL(0U);
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __DSB();
    __ISB();
    __enable_irq();
    __DSB();
    __ISB();

    ((BootAppTestEntryPoint) reset_handler)();
}

void Boot_App_Init(void) {
    g_boot_test_started = 0U;
    g_boot_test_stage = 0U;
    g_boot_test_app_base = 0U;
    g_boot_test_app_sp = 0U;
    g_boot_test_app_reset = 0U;
    g_boot_test_app_nmi = 0U;
    g_boot_test_app_hardfault = 0U;
    g_boot_test_vector_valid = 0U;
    g_boot_test_vector_error = 0U;
}

void Boot_App_Process(void) {
    uint32_t stack_pointer = 0U;
    uint32_t reset_handler = 0U;

    if (g_boot_test_started != 0U) {
        return;
    }

    g_boot_test_started = 1U;

    if (Boot_App_Test_ReadAppVector(BOOT_DEFAULT_APP_BASE, &stack_pointer, &reset_handler) ==
            BOOT_ERR_NONE &&
        g_boot_test_vector_valid != 0U) {
        Boot_App_Test_JumpToApp(BOOT_DEFAULT_APP_BASE, stack_pointer, reset_handler);
    }

    while (1) {
    }
}
