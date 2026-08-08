/**
 * @file vector_validation.c
 * @brief Cortex-M7 应用向量表的启动前校验实现。
 *
 * Bootloader 在切换 VTOR、MSP 并跳转到外部 Flash 中的 Application 前，使用本文件
 * 校验向量表的前两个字：初始主堆栈指针和 Reset Handler。校验只依赖调用者提供的
 * 分区和 SRAM 白名单，不直接依赖具体 BSP 地址，从而保持服务层的平台无关性。
 */
#include "services/capability/vector_validation.h"

#include <stddef.h>

#include "services/capability/checked_arithmetic.h"

/**
 * @brief 判断一个初始 MSP 是否落在允许的 SRAM 区域边界内。
 *
 * @param address 待验证的初始主堆栈指针值。
 * @param region 允许使用的 SRAM 区域，调用者保证其非 NULL。
 *
 * @return 1 表示地址满足该区域的降栈边界规则；0 表示区域长度导致地址计算溢出，
 *         或地址不在允许范围内。
 *
 * Cortex-M 的堆栈向低地址增长，初始 MSP 可以等于区域的排他结束地址，随后第一次
 * 压栈才进入可寻址 SRAM；但不能等于起始地址，因为该位置没有向下增长空间。
 */
static int StackPointerInRegion(uint32_t address, const memory_region_t *region)
{
    uint32_t end;

    if (!CheckedArithmetic_RangeEndU32(region->start_address, region->size, &end))
    {
        /* 配置的 SRAM 区域本身不可表示时，不应信任其中任何地址。 */
        return 0;
    }

    /* 降栈可从区域排他结束地址开始，但不能从起始地址或区域外开始。 */
    return (address > region->start_address) && (address <= end);
}

/**
 * @brief 校验 Application 向量表是否能够安全作为 Cortex-M7 的启动入口。
 *
 * @param vectors 待校验向量表的 initial_msp 与 reset_handler，不能为 NULL。
 * @param app_region Application 所在的可映射启动分区，不能为 NULL。
 * @param app_image_size 实际 Application 镜像长度，必须非零且不超过分区容量。
 * @param sram_regions 可供 Application 初始 MSP 使用的 SRAM 区域数组，不能为 NULL。
 * @param sram_region_count @p sram_regions 中的有效元素数量，必须非零。
 *
 * @return FIRMWARE_STATUS_OK 表示 MSP、Reset Handler 和镜像范围均有效；
 *         FIRMWARE_STATUS_INVALID_ARGUMENT 表示必要指针或 SRAM 区域数量无效；
 *         FIRMWARE_STATUS_OUT_OF_RANGE 表示镜像长度、MSP 对齐/位置、Thumb 标志或
 *         Reset Handler 位置不符合启动约束。
 *
 * Reset Handler 必须保留 Thumb 位，并且清除 Thumb 位后的地址必须位于实际镜像长度
 * 内，而不是仅位于整个 Application 分区内，防止跳转到镜像外的任意 XIP 地址。
 */
firmware_status_t VectorValidation_Validate(const vector_table_values_t *vectors,
                                            const boot_region_t *app_region,
                                            uint32_t app_image_size,
                                            const memory_region_t *sram_regions,
                                            uint32_t sram_region_count)
{
    uint32_t index;
    uint32_t reset_address;
    int msp_valid = 0;

    if ((vectors == NULL) || (app_region == NULL) || (sram_regions == NULL) ||
        (sram_region_count == 0U))
    {
        /* 没有完整的向量、分区或 SRAM 白名单信息，无法作出安全判定。 */
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((app_image_size == 0U) || (app_image_size > app_region->capacity_bytes))
    {
        /* 实际镜像必须是 Application 分区内的非空连续范围。 */
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    /* 初始 MSP 只要命中任一经配置的 SRAM 区域即可。 */
    for (index = 0U; index < sram_region_count; ++index)
    {
        if (StackPointerInRegion(vectors->initial_msp, &sram_regions[index]))
        {
            msp_valid = 1;
            break;
        }
    }
    if (((vectors->initial_msp & 0x7U) != 0U) || (msp_valid == 0))
    {
        /* AAPCS 要求公共接口处的栈保持 8 字节对齐。 */
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if ((vectors->reset_handler & 0x1U) == 0U)
    {
        /* Cortex-M 只能以 Thumb 状态执行 Reset Handler。 */
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    /* 比较时去除 Thumb 标志位，保留原值供后续跳转时使用。 */
    reset_address = vectors->reset_handler & ~0x1UL;
    if ((reset_address < app_region->mapped_address) ||
        ((reset_address - app_region->mapped_address) >= app_image_size))
    {
        /* 入口必须位于已验证镜像，而非未使用分区空间或其他映射区域。 */
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    return FIRMWARE_STATUS_OK;
}
