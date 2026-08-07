#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "firmware/status.h"
#include "services/capability/checked_arithmetic.h"
#include "services/capability/vector_validation.h"
#include "services/capability/version_policy.h"
#include "services/common/runtime_layout.h"

static void TestCheckedArithmetic(void)
{
    uint32_t result;

    assert(CheckedArithmetic_AddU32(10U, 20U, &result) != 0);
    assert(result == 30U);
    assert(CheckedArithmetic_AddU32(UINT32_MAX, 1U, &result) == 0);
    assert(CheckedArithmetic_MultiplyU32(4096U, 256U, &result) != 0);
    assert(result == 1048576U);
    assert(CheckedArithmetic_MultiplyU32(UINT32_MAX, 2U, &result) == 0);
    assert(CheckedArithmetic_RangeEndU32(0x1000U, 0U, &result) == 0);
}

static void TestRuntimeLayout(void)
{
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();

    assert(layout != NULL);
    assert(layout->app_offset == 0x000000UL);
    assert(layout->app_xip_base == 0x90000000UL);
    assert(layout->app_max_size == 0x00100000UL);
    assert(layout->gui_offset == 0x00200000UL);
    assert(layout->gui_mmap_base == 0x90200000UL);
    assert(layout->gui_max_size == 0x00800000UL);
}

static void TestVersionPolicy(void)
{
    const release_version_t current = {1U, 2U, 3U};
    const release_version_t newer = {1U, 3U, 0U};
    const release_version_t older = {1U, 2U, 2U};

    assert(VersionPolicy_Compare(&current, &current) == 0);
    assert(VersionPolicy_IsUpgrade(&current, &newer) != 0);
    assert(VersionPolicy_IsUpgrade(&current, &older) == 0);
}

static void TestVectorValidation(void)
{
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();
    const boot_region_t app_region = {
        layout->app_offset, layout->app_xip_base, layout->app_max_size};
    const memory_region_t sram[] = {
        {0x20000000UL, 0x00020000UL},
        {0x24000000UL, 0x00080000UL},
    };
    vector_table_values_t vectors = {
        .initial_msp = 0x24080000UL,
        .reset_handler = 0x90000101UL,
    };

    assert(VectorValidation_Validate(
               &vectors, &app_region, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) == FIRMWARE_STATUS_OK);

    vectors.reset_handler &= ~0x1UL;
    assert(VectorValidation_Validate(
               &vectors, &app_region, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);

    vectors.reset_handler = 0x90000101UL;
    vectors.initial_msp = 0x10000000UL;
    assert(VectorValidation_Validate(
               &vectors, &app_region, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);
}

int main(void)
{
    TestCheckedArithmetic();
    TestRuntimeLayout();
    TestVersionPolicy();
    TestVectorValidation();
    return 0;
}
