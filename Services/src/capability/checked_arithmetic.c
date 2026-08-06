/**
 * @file checked_arithmetic.c
 * @brief Overflow-safe 32-bit arithmetic implementation.
 */
#include "services/capability/checked_arithmetic.h"

#include <limits.h>
#include <stddef.h>

int CheckedArithmetic_AddU32(uint32_t lhs, uint32_t rhs, uint32_t *result)
{
    if ((result == NULL) || (lhs > (UINT32_MAX - rhs)))
    {
        return 0;
    }

    *result = lhs + rhs;
    return 1;
}

int CheckedArithmetic_MultiplyU32(uint32_t lhs, uint32_t rhs, uint32_t *result)
{
    if ((result == NULL) || ((rhs != 0U) && (lhs > (UINT32_MAX / rhs))))
    {
        return 0;
    }

    *result = lhs * rhs;
    return 1;
}

int CheckedArithmetic_RangeEndU32(uint32_t start, uint32_t size, uint32_t *end)
{
    if (size == 0U)
    {
        return 0;
    }

    return CheckedArithmetic_AddU32(start, size, end);
}
