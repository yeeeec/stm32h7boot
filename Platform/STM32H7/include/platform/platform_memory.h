/**
 * @file platform_memory.h
 * @brief ARM memory and instruction barrier wrappers.
 */
#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

/** Ensure explicit memory accesses are observed in the required order. */
void Platform_MemoryDataBarrier(void);

/** Wait for explicit memory accesses to complete before continuing. */
void Platform_MemorySyncBarrier(void);

/** Ensure subsequent instruction fetches observe the updated execution state. */
void Platform_MemoryInstructionBarrier(void);

#endif
