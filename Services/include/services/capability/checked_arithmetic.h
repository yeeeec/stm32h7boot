/**
 * @file checked_arithmetic.h
 * @brief 用于发布包和分区校验的无溢出算术能力。
 *
 * 函数返回非零仅表示运算在 uint32_t 范围内完成；result 为 NULL 或结果会溢出
 * 时返回零，调用者不得使用未写入的结果。
 */
#ifndef SERVICES_CHECKED_ARITHMETIC_H
#define SERVICES_CHECKED_ARITHMETIC_H

#include <stdint.h>

/** @brief 无回绕地计算 lhs + rhs。 */
int CheckedArithmetic_AddU32(uint32_t lhs, uint32_t rhs, uint32_t *result);

/** @brief 无回绕地计算 lhs * rhs。 */
int CheckedArithmetic_MultiplyU32(uint32_t lhs, uint32_t rhs, uint32_t *result);

/** @brief 计算 [start, start + size) 的排他结束地址，并拒绝零长度和溢出。 */
int CheckedArithmetic_RangeEndU32(uint32_t start, uint32_t size, uint32_t *end);

#endif
