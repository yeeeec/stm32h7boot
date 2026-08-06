/**
 * @file checked_arithmetic.h
 * @brief Overflow-safe arithmetic used by package and partition validation.
 */
#ifndef SERVICES_CHECKED_ARITHMETIC_H
#define SERVICES_CHECKED_ARITHMETIC_H

#include <stdint.h>

/** Add two 32-bit values without wrapping. */
int CheckedArithmetic_AddU32(uint32_t lhs, uint32_t rhs, uint32_t *result);

/** Multiply two 32-bit values without wrapping. */
int CheckedArithmetic_MultiplyU32(uint32_t lhs, uint32_t rhs, uint32_t *result);

/** Calculate an exclusive range end without wrapping or accepting zero length. */
int CheckedArithmetic_RangeEndU32(uint32_t start, uint32_t size, uint32_t *end);

#endif
