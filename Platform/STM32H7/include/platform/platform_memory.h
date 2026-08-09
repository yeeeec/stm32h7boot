/**
 * @file platform_memory.h
 * @brief 硬件状态切换边界使用的 ARM 顺序原语。
 *
 * 这些 Wrapper 表达访问顺序要求；它们不执行 Cache Maintenance，也不能替代
 * 外设完成状态检查。
 */
#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

/**
 * @brief 将显式内存访问排序到本调用之后的访问之前。
 *
 * 这是 DMB 排序点，不会等待所有未完成传输到达最终目的地。
 */
void Platform_MemoryDataBarrier(void);

/**
 * @brief 在执行后续工作前完成显式内存访问。
 *
 * 这是 DSB 完成点，用于 Reset 或硬件 Remap 之前。
 */
void Platform_MemorySyncBarrier(void);

/**
 * @brief 执行上下文变化后刷新指令流水线。
 *
 * 这是 ISB，通常与影响后续指令取指的 Control Register 或 Vector Table
 * 变更配对使用。
 */
void Platform_MemoryInstructionBarrier(void);

#endif
