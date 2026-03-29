#include "boot_info.h"

#include <stddef.h>
#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_simple_flash.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    s_BootInfo info;
    uint8_t padding[BOOT_INFO_RECORD_SIZE - sizeof(s_BootInfo)];
} BootInfoRecord;

_Static_assert(sizeof(s_BootInfo) <= BOOT_INFO_RECORD_SIZE,
               "BootInfo must fit within a flash record");

typedef struct
{
    s_BootInfo latest;
    uint32_t latest_index;
    uint32_t next_free_index;
    bool has_valid_record;
    bool has_non_empty_record;
} BootInfoScanResult;

static bool Boot_Info_IsPendingSlotValueValid(uint8_t slot)
{
    return (slot == SLOT_NONE) || Boot_Info_IsSlotValueValid(slot);
}

static bool Boot_Info_IsUpgradeStateValid(uint8_t state)
{
    switch (state)
    {
        case UPGRADE_IDLE:
        case UPGRADE_READY:
        case UPGRADE_TESTING:
        case UPGRADE_ROLLBACK:
        case UPGRADE_SUCCESS:
            return true;

        default:
            return false;
    }
}

static bool Boot_Info_IsRollbackReasonValid(uint8_t reason)
{
    switch (reason)
    {
        case ROLLBACK_NONE:
        case ROLLBACK_CRC_ERROR:
        case ROLLBACK_BOOT_TIMEOUT:
        case ROLLBACK_BOOT_OVERFLOW:
        case ROLLBACK_WDG_RESET:
        case ROLLBACK_APP_FAULT:
            return true;

        default:
            return false;
    }
}

static bool Boot_Info_IsFlashRecordBlank(uint32_t index)
{
    const uint8_t *record_ptr =
        (const uint8_t *)(uintptr_t)(BOOT_INFO_BASE + (index * BOOT_INFO_RECORD_SIZE));
    uint32_t offset;

    for (offset = 0U; offset < BOOT_INFO_RECORD_SIZE; ++offset)
    {
        if (record_ptr[offset] != 0xFFU)
        {
            return false;
        }
    }

    return true;
}

static void Boot_Info_Scan(BootInfoScanResult *result)
{
    uint32_t index;

    if (result == NULL)
    {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->next_free_index = BOOT_INFO_RECORD_COUNT;

    for (index = 0U; index < BOOT_INFO_RECORD_COUNT; ++index)
    {
        const BootInfoRecord *record =
            (const BootInfoRecord *)(uintptr_t)(BOOT_INFO_BASE + (index * BOOT_INFO_RECORD_SIZE));

        if (Boot_Info_IsFlashRecordBlank(index) != false)
        {
            result->next_free_index = index;
            break;
        }

        result->has_non_empty_record = true;
        if (Boot_Info_IsStructValid(&record->info) == false)
        {
            continue;
        }

        if ((result->has_valid_record == false) || (record->info.seq >= result->latest.seq))
        {
            result->latest          = record->info;
            result->latest_index    = index;
            result->has_valid_record = true;
        }
    }
}

uint32_t Boot_Info_CalcStructCrc(const s_BootInfo *info)
{
    if (info == NULL)
    {
        return 0U;
    }

    return Boot_Crc32_IsoCalc(info, offsetof(s_BootInfo, crc));
}

bool Boot_Info_IsSlotValueValid(uint8_t slot)
{
    return (slot == SLOT_A) || (slot == SLOT_B);
}

bool Boot_Info_IsStructValid(const s_BootInfo *info)
{
    if (info == NULL)
    {
        return false;
    }

    if (info->magic != BOOT_INFO_MAGIC)
    {
        return false;
    }

    if (Boot_Info_IsSlotValueValid(info->active_slot) == false)
    {
        return false;
    }

    if (Boot_Info_IsPendingSlotValueValid(info->pending_slot) == false)
    {
        return false;
    }

    if ((info->confirmed != BOOT_NOT_CONFIRMED) && (info->confirmed != BOOT_CONFIRMED))
    {
        return false;
    }

    if (Boot_Info_IsUpgradeStateValid(info->upgrade_state) == false)
    {
        return false;
    }

    if (Boot_Info_IsRollbackReasonValid(info->rollback_reason) == false)
    {
        return false;
    }

    return (Boot_Info_CalcStructCrc(info) == info->crc);
}

const char *Boot_Info_SlotToString(uint8_t slot)
{
    switch (slot)
    {
        case SLOT_A:
            return "A";

        case SLOT_B:
            return "B";

        case SLOT_NONE:
            return "none";

        default:
            return "invalid";
    }
}

void Boot_Info_InitDefaults(s_BootInfo *info)
{
    if (info == NULL)
    {
        return;
    }

    memset(info, 0, sizeof(*info));
    info->magic          = BOOT_INFO_MAGIC;
    info->active_slot    = SLOT_A;
    info->pending_slot   = SLOT_NONE;
    info->confirmed      = BOOT_CONFIRMED;
    info->boot_count     = 0U;
    info->max_boot_count = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
    info->upgrade_state  = UPGRADE_IDLE;
    info->rollback_reason = ROLLBACK_NONE;
}

BootError Boot_Info_Load(s_BootInfo *info)
{
    BootInfoScanResult scan;

    if (info == NULL)
    {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    Boot_Info_Scan(&scan);
    if (scan.has_valid_record != false)
    {
        *info = scan.latest;
        return BOOT_ERR_NONE;
    }

    if (scan.has_non_empty_record != false)
    {
        return BOOT_ERR_CTRL_CRC;
    }

    return BOOT_ERR_CTRL_NOT_FOUND;
}

BootError Boot_Info_Store(s_BootInfo *info)
{
    BootInfoScanResult scan;
    BootInfoRecord record;
    uint32_t address;

    if (info == NULL)
    {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    Boot_Info_Scan(&scan);

    info->magic = BOOT_INFO_MAGIC;
    if (info->max_boot_count == 0U)
    {
        info->max_boot_count = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
    }

    if (scan.has_valid_record != false)
    {
        info->seq = scan.latest.seq + 1U;
    }
    else if (info->seq == 0U)
    {
        info->seq = 1U;
    }

    info->crc = Boot_Info_CalcStructCrc(info);

    memset(&record, 0xFF, sizeof(record));
    record.info = *info;

    if (scan.next_free_index >= BOOT_INFO_RECORD_COUNT)
    {
        BootError error = Boot_SimpleFlash_EraseInfoRegion();
        if (error != BOOT_ERR_NONE)
        {
            return error;
        }

        address = BOOT_INFO_BASE;
    }
    else
    {
        address = BOOT_INFO_BASE + (scan.next_free_index * BOOT_INFO_RECORD_SIZE);
    }

    {
        BootError error = Boot_SimpleFlash_Write(address, &record, sizeof(record));
        if (error != BOOT_ERR_NONE)
        {
            return error;
        }
    }

    return BOOT_ERR_NONE;
}

uint8_t Boot_Info_DetectRunningSlot(void)
{
    uint32_t vector_base = SCB->VTOR;
    const BootSimpleFlashRegion *slot_region;

    slot_region = Boot_SimpleFlash_GetSlotRegion(SLOT_A);
    if ((slot_region != NULL) && (vector_base == slot_region->base))
    {
        return SLOT_A;
    }

    slot_region = Boot_SimpleFlash_GetSlotRegion(SLOT_B);
    if ((slot_region != NULL) && (vector_base == slot_region->base))
    {
        return SLOT_B;
    }

    return SLOT_NONE;
}

BootError Boot_Info_ConfirmRunningImage(void)
{
    s_BootInfo info;
    uint8_t running_slot;
    BootError error;

    running_slot = Boot_Info_DetectRunningSlot();
    if (Boot_Info_IsSlotValueValid(running_slot) == false)
    {
        return BOOT_ERR_IMAGE_SLOT;
    }

    error = Boot_Info_Load(&info);
    if ((error != BOOT_ERR_NONE) && (error != BOOT_ERR_CTRL_NOT_FOUND) && (error != BOOT_ERR_CTRL_CRC))
    {
        return error;
    }

    if (error != BOOT_ERR_NONE)
    {
        Boot_Info_InitDefaults(&info);
    }

    if ((running_slot == SLOT_A) && (info.pending_slot == SLOT_A) && (info.pending_size != 0U) &&
        (info.pending_crc != 0U))
    {
        info.app_a_size = info.pending_size;
        info.app_a_crc  = info.pending_crc;
    }

    info.active_slot     = running_slot;
    info.pending_slot    = SLOT_NONE;
    info.confirmed       = BOOT_CONFIRMED;
    info.boot_count      = 0U;
    info.upgrade_state   = UPGRADE_SUCCESS;
    info.rollback_reason = ROLLBACK_NONE;
    info.pending_size    = 0U;
    info.pending_crc     = 0U;

    return Boot_Info_Store(&info);
}
