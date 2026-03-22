#include "platform/system.h"

/** @brief 引用所有子模块头文件 */
#include "platform/adc.h"
#include "platform/clock.h"
#include "platform/config.h"
#include "platform/crc.h"
#include "platform/dma.h"
#include "platform/gpio.h"
#include "platform/i2c.h"
#include "platform/iflash.h"
#include "platform/iwdg.h"
#include "platform/timer.h"
#include "platform/uart.h"


/**
 * @brief 引用芯片特定的系统头文件
 * @note  为了 SystemInit；若使用 GD32 标准库，通常不需要在此显式引用，后端会处理
 */

/**
 * @brief 平台层总初始化
 * @note  负责按正确顺序初始化时钟、中断、GPIO、通信总线和外设
 */
void system_init_platform(void) {
    platform_system_init();
    platform_clock_init();

    /** ------------------------------------------------------------------
     * 1. 基础系统初始化 (最为优先)
     * ------------------------------------------------------------------ */

    platform_dma_init();

    /** 初始化硬件定时器 (用于系统时基/任务调度) */
    /** 假设我们在 config.h 定义了系统节拍周期，例如 10ms */
    platform_timer_base_init(10);
    platform_timer_start();
    /** ------------------------------------------------------------------
     * 2. IO 与 总线初始化 (通信的基础)
     * ------------------------------------------------------------------ */

    /** GPIO 初始化 (必须在 UART/I2C 之前，或者由后端内部处理) */
    /** 我们的设计中，gpio_backend.c 会处理时钟，所以这里调用即可 */
    platform_gpio_init();

    /** platform_uart_init(PLAT_UART_SENSOR, 9600); */
    /** platform_uart_init(PLAT_UART_DISPLAY, 9600); */

    /** I2C 初始化 */
    platform_i2c_init();

    /** ------------------------------------------------------------------
     * 3. 功能外设初始化
     * ------------------------------------------------------------------ */

    /** Flash 驱动初始化 (解锁/清除标志) */
    platform_flash_init();

    /** CRC 单元初始化 */
    platform_crc_init();

    /** ADC 初始化 (可能需要校准，比较耗时) */
    platform_adc_init();

    platform_iwdg_init(2000);

    /** ------------------------------------------------------------------
     * 4. 全局中断使能 (最后一步)
     * ------------------------------------------------------------------ */
    /** __enable_irq(); // 通常启动文件或 RTOS 会管理，裸机可在此显式开启 */
}

void platform_process(void) {
    platform_iwdg_feed();
}

#if defined(USE_FREERTOS)
/** --- RTOS 模式 --- */
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 进入临界区（支持嵌套调用）
 */
void system_enter_critical_section(void) {
    /** FreeRTOS 的 taskENTER_CRITICAL 内部已经处理了嵌套计数 */
    /** 注意：如果在中断中使用，需要用 taskENTER_CRITICAL_FROM_ISR，
       但通常临界区用于任务间保护。如果需要在 ISR 中使用，需另行处理。 */
    if (!xPortIsInsideInterrupt()) {
        taskENTER_CRITICAL();
    }
}

/**
 * @brief 退出临界区
 */
void system_exit_critical_section(void) {
    if (!xPortIsInsideInterrupt()) {
        taskEXIT_CRITICAL();
    }
}

#else
/** --- 裸机模式 (Bare Metal) --- */
#include "cmsis_gcc.h"

/** 静态变量记录嵌套深度 */
/** @brief 临界区嵌套深度计数（裸机模式） */
static volatile uint32_t g_lock_nesting = 0;

void system_enter_critical_section(void) {
    __disable_irq();  /** 1. 先关中断 */
    g_lock_nesting++; /** 2. 再增加计数 */
}

void system_exit_critical_section(void) {
    if (g_lock_nesting > 0) {
        g_lock_nesting--;
    }

    /** 只有当所有嵌套都退出时，才真正打开中断 */
    if (g_lock_nesting == 0) {
        __enable_irq();
    }
}

#endif
