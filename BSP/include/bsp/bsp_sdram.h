/**
 * @file bsp_sdram.h
 * @brief 通过 FMC Controller 初始化板级 SDRAM。
 */
#ifndef BSP_SDRAM_H
#define BSP_SDRAM_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uintptr_t base_address;
        size_t size_bytes;
        uintptr_t framebuffer_address[2];
        size_t framebuffer_region_size_bytes;
        uintptr_t app_address;
        size_t app_size_bytes;
    } bsp_sdram_layout_t;

    /**
     * @brief 初始化板级 SDRAM 时序和模式配置。
     *
     * @return 成功时返回 FIRMWARE_STATUS_OK。
     * @return CubeMX SDRAM Handle 处于 Reset 时返回 FIRMWARE_STATUS_INVALID_STATE。
     * @return 其他情况返回 Controller 或传输失败状态。
     *
     * @pre 生成的 FMC/SDRAM Handle 已完成初始化。
     * @pre 本函数成功前，调用者不得访问外部 SDRAM。
     */
    firmware_status_t BSP_SdramInit(void);

    /** @brief 仅在 SDRAM JEDEC 序列和刷新配置均成功后返回非零。 */
    int BSP_SdramIsReady(void);

    /** @brief 返回只读的板级 SDRAM 地址布局。 */
    const bsp_sdram_layout_t *BSP_SdramGetLayout(void);

    /**
     * @brief 对已发布 SDRAM 范围执行 D-Cache clean。
     * @pre 地址和大小满足 Platform 32-byte cache-line 对齐约束。
     */
    firmware_status_t BSP_SdramDCacheClean(uintptr_t address, size_t size);

    /** @brief 对已发布 SDRAM 范围执行 D-Cache invalidate。 */
    firmware_status_t BSP_SdramDCacheInvalidate(uintptr_t address, size_t size);

    /** @brief 对已发布 SDRAM 范围执行 D-Cache clean 后 invalidate。 */
    firmware_status_t BSP_SdramDCacheCleanInvalidate(uintptr_t address, size_t size);

    /**
     * @brief 在 SDRAM app region 内执行破坏性 address-pattern 自检。
     *
     * 调用方必须保证测试范围当前未被 heap、静态对象、task 或 DMA 使用。地址和
     * 大小必须按 32-byte cache line 对齐。
     */
    firmware_status_t BSP_SdramTest(uintptr_t address, size_t size);

#ifdef __cplusplus
}
#endif

#endif
