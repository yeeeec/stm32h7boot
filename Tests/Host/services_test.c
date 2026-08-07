#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "firmware/status.h"
#include "services/capability/checked_arithmetic.h"
#include "services/capability/slot_policy.h"
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

static void TestSlotPolicy(void)
{
    boot_pair_layout_t layout;
    boot_pair_t inactive_pair;

    assert(SlotPolicy_GetPairLayout(BOOT_PAIR_1, &layout) == FIRMWARE_STATUS_OK);
    assert(layout.app.flash_offset == 0x00000000UL);
    assert(layout.app.mapped_address == 0x90000000UL);
    assert(layout.app.capacity_bytes == 0x00100000UL);
    assert(layout.gui.flash_offset == 0x00200000UL);
    assert(layout.gui.capacity_bytes == 0x00800000UL);
    assert(SlotPolicy_SelectInactivePair(BOOT_PAIR_1, &inactive_pair) ==
           FIRMWARE_STATUS_OK);
    assert(inactive_pair == BOOT_PAIR_2);
    assert(SlotPolicy_ValidateImageSize(&layout.app, layout.app.capacity_bytes) ==
           FIRMWARE_STATUS_OK);
    assert(SlotPolicy_ValidateImageSize(&layout.app, 0U) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);
    assert(SlotPolicy_ContainsRange(
               &layout.gui, layout.gui.flash_offset, layout.gui.capacity_bytes) != 0);
    assert(SlotPolicy_ContainsRange(
               &layout.gui, layout.gui.flash_offset - 1U, 1U) == 0);
    assert(SlotPolicy_ValidateStorageGeometry(
               SLOT_POLICY_FLASH_CAPACITY_BYTES, SLOT_POLICY_ERASE_SIZE_BYTES) ==
           FIRMWARE_STATUS_OK);
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
    boot_pair_layout_t layout;
    const memory_region_t sram[] = {
        {0x20000000UL, 0x00020000UL},
        {0x24000000UL, 0x00080000UL},
    };
    vector_table_values_t vectors = {
        .initial_msp = 0x24080000UL,
        .reset_handler = 0x90000101UL,
    };

    assert(SlotPolicy_GetPairLayout(BOOT_PAIR_1, &layout) == FIRMWARE_STATUS_OK);
    assert(VectorValidation_Validate(
               &vectors, &layout.app, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) == FIRMWARE_STATUS_OK);

    vectors.reset_handler &= ~0x1UL;
    assert(VectorValidation_Validate(
               &vectors, &layout.app, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);

    vectors.reset_handler = 0x90000101UL;
    vectors.initial_msp = 0x10000000UL;
    assert(VectorValidation_Validate(
               &vectors, &layout.app, 0x1000U, sram,
               (uint32_t)(sizeof(sram) / sizeof(sram[0]))) ==
           FIRMWARE_STATUS_OUT_OF_RANGE);
}

int main(void)
{
    TestCheckedArithmetic();
    TestSlotPolicy();
    TestRuntimeLayout();
    TestVersionPolicy();
    TestVectorValidation();
    return 0;
}
