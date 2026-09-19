#ifndef PLATFORM_CPU_H
#define PLATFORM_CPU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool PlatformCpu_IsValidStackPointer(uint32_t stack_pointer);
    _Noreturn void PlatformCpu_Jump(uint32_t vector_address);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CPU_H */
