/**
 * @file checked_arithmetic.c
 * @brief 用于分区和镜像边界校验的无溢出 32 位算术实现。
 *
 * 固件地址、长度和偏移量均以 uint32_t 表示。直接相加或相乘发生回绕时，
 * 可能使越界镜像被误判为位于合法区域；本文件将运算结果与溢出检查绑定，
 * 供调用方在继续进行地址比较前明确处理失败情况。
 */
#include "services/capability/checked_arithmetic.h"

#include <limits.h>
#include <stddef.h>

/**
 * @brief 对两个无符号 32 位整数做不回绕加法。
 *
 * @param lhs 加数。
 * @param rhs 加数。
 * @param result 成功时接收和的输出地址，不能为 NULL。
 *
 * @return 1 表示已将精确结果写入 @p result；0 表示输出地址无效或结果超过
 *         UINT32_MAX，且不会写入结果。
 *
 * 通过比较 @p lhs 与 UINT32_MAX - @p rhs 在实际执行加法前检测上溢，
 * 因此不会依赖 C 无符号回绕后的值进行后续边界判断。
 */
int CheckedArithmetic_AddU32(uint32_t lhs, uint32_t rhs, uint32_t *result)
{
    if ((result == NULL) || (lhs > (UINT32_MAX - rhs)))
    {
        /* NULL 输出或上溢均不能向调用者提供可信结果。 */
        return 0;
    }

    *result = lhs + rhs;
    return 1;
}

/**
 * @brief 对两个无符号 32 位整数做不回绕乘法。
 *
 * @param lhs 被乘数。
 * @param rhs 乘数。
 * @param result 成功时接收积的输出地址，不能为 NULL。
 *
 * @return 1 表示已将精确结果写入 @p result；0 表示输出地址无效或乘积超过
 *         UINT32_MAX，且不会写入结果。
 *
 * 当 @p rhs 非零时，先以 UINT32_MAX / rhs 给出 @p lhs 的最大可接受值；
 * rhs 为零时乘积必为零，无需执行除法。
 */
int CheckedArithmetic_MultiplyU32(uint32_t lhs, uint32_t rhs, uint32_t *result)
{
    if ((result == NULL) || ((rhs != 0U) && (lhs > (UINT32_MAX / rhs))))
    {
        /* 乘法结果不可表示时必须拒绝，不能返回回绕后的低 32 位。 */
        return 0;
    }

    *result = lhs * rhs;
    return 1;
}

/**
 * @brief 计算半开地址范围 [start, start + size) 的排他结束地址。
 *
 * @param start 范围起始地址。
 * @param size 范围长度，必须大于零。
 * @param end 成功时接收排他结束地址的输出地址。
 *
 * @return 1 表示范围长度有效且结束地址可由 uint32_t 表示；0 表示长度为零、
 *         输出地址无效或 start + size 溢出。
 *
 * 结束地址采用排他上界，便于后续以 "address < end" 判断地址是否处于范围中。
 */
int CheckedArithmetic_RangeEndU32(uint32_t start, uint32_t size, uint32_t *end)
{
    if (size == 0U)
    {
        /* 零长度不能构成可验证的地址范围。 */
        return 0;
    }

    return CheckedArithmetic_AddU32(start, size, end);
}
