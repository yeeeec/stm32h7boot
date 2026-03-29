#include "boot_app.h"

#include <string.h>

#include "boot_handoff.h"
#include "boot_info.h"
#include "boot_log.h"
#include "boot_simple_flash.h"
#include "boot_simple_jump.h"
#include "boot_simple_manifest.h"
#include "boot_simple_upgrade.h"
#include "boot_usb.h"
#include "platform/boot_platform.h"
#include "stm32h7xx_hal.h"

typedef enum {
    BOOT_APP_STATE_INIT = 0,
    BOOT_APP_STATE_USB_SCAN,
    BOOT_APP_STATE_IMAGE_LOAD,
    BOOT_APP_STATE_UPGRADE,
    BOOT_APP_STATE_JUMP,
    BOOT_APP_STATE_RECOVERY,
    BOOT_APP_STATE_FATAL
} BootAppState;

typedef struct {
    BootAppState state;
    BootError last_error;
    BootAppImageInfo image_info;
    s_BootInfo boot_info;
    uint32_t reset_flags;
    uint8_t jump_slot;
    uint8_t allow_upgrade_scan;
    uint8_t recovery_retry_blocked;
    BootError recovery_block_error;
} BootAppContext;

static BootAppContext g_boot_app;

static void Boot_App_FeedWatchdogForever(void) {
    while (1) {
        Boot_Platform_FeedWatchdog();
    }
}

static void Boot_App_EnterFatal(BootError error) {
    g_boot_app.state      = BOOT_APP_STATE_FATAL;
    g_boot_app.last_error = error;
    Boot_Handoff_SetError((uint32_t)error);
    Boot_Handoff_LogCurrent("Fatal handoff");
    LOG_ERROR(BOOT_LOG_TAG, "Fatal boot error: %s", Boot_ErrorToString(error));
    Boot_App_FeedWatchdogForever();
}

static void Boot_App_BlockRecoveryRetry(BootError error) {
    if (g_boot_app.jump_slot != SLOT_NONE) {
        return;
    }

    if (g_boot_app.recovery_retry_blocked == 0U) {
        LOG_WARN(BOOT_LOG_TAG, "Recovery retry paused until upgrade media is removed: %s",
                 Boot_ErrorToString(error));
    }

    g_boot_app.recovery_retry_blocked = 1U;
    g_boot_app.recovery_block_error   = error;
}

static void Boot_App_ClearRecoveryRetryBlock(const char *reason) {
    if (g_boot_app.recovery_retry_blocked == 0U) {
        return;
    }

    LOG_INFO(BOOT_LOG_TAG, "Recovery retry re-armed (%s), previous failure: %s",
             (reason != NULL) ? reason : "unknown",
             Boot_ErrorToString(g_boot_app.recovery_block_error));
    g_boot_app.recovery_retry_blocked = 0U;
    g_boot_app.recovery_block_error   = BOOT_ERR_NONE;
}

static int Boot_App_IsWatchdogReset(uint32_t reset_flags) {
    return ((reset_flags & RCC_RSR_IWDG1RSTF) != 0U) || ((reset_flags & RCC_RSR_WWDG1RSTF) != 0U);
}

static BootError Boot_App_SaveBootInfo(const char *reason) {
    BootError error;

    g_boot_app.boot_info.last_reset_reason = g_boot_app.reset_flags;
    error                                  = Boot_Info_Store(&g_boot_app.boot_info);
    if (error != BOOT_ERR_NONE) {
        LOG_WARN(BOOT_LOG_TAG, "BootInfo save failed (%s): %s", reason,
                 Boot_ErrorToString(error));
    } else {
        LOG_INFO(BOOT_LOG_TAG,
                 "BootInfo saved (%s): active=%s pending=%s confirmed=%u boot_count=%u state=%u",
                 reason, Boot_Info_SlotToString(g_boot_app.boot_info.active_slot),
                 Boot_Info_SlotToString(g_boot_app.boot_info.pending_slot),
                 (unsigned int)g_boot_app.boot_info.confirmed,
                 (unsigned int)g_boot_app.boot_info.boot_count,
                 (unsigned int)g_boot_app.boot_info.upgrade_state);
    }

    return error;
}

static void Boot_App_SetSlotMetadata(uint8_t slot, uint32_t size, uint32_t crc32) {
    if (slot == SLOT_A) {
        g_boot_app.boot_info.version_a  = 0U;
        g_boot_app.boot_info.app_a_size = size;
        g_boot_app.boot_info.app_a_crc  = crc32;
    } else if (slot == SLOT_B) {
        g_boot_app.boot_info.version_b  = 0U;
        g_boot_app.boot_info.app_b_size = size;
        g_boot_app.boot_info.app_b_crc  = crc32;
    }
}

static uint32_t Boot_App_GetSlotExpectedSize(uint8_t slot) {
    if (slot == SLOT_A) {
        return g_boot_app.boot_info.app_a_size;
    }

    if (slot == SLOT_B) {
        return g_boot_app.boot_info.app_b_size;
    }

    return 0U;
}

static uint32_t Boot_App_GetSlotExpectedCrc(uint8_t slot) {
    if (slot == SLOT_A) {
        return g_boot_app.boot_info.app_a_crc;
    }

    if (slot == SLOT_B) {
        return g_boot_app.boot_info.app_b_crc;
    }

    return 0U;
}

static bool Boot_App_HasSlotMetadata(uint8_t slot) {
    return (Boot_App_GetSlotExpectedSize(slot) != 0U) && (Boot_App_GetSlotExpectedCrc(slot) != 0U);
}

static void Boot_App_SetPendingMetadata(uint32_t size, uint32_t crc32) {
    g_boot_app.boot_info.pending_size = size;
    g_boot_app.boot_info.pending_crc  = crc32;
}

static void Boot_App_ClearPendingMetadata(void) {
    g_boot_app.boot_info.pending_size = 0U;
    g_boot_app.boot_info.pending_crc  = 0U;
}

static bool Boot_App_HasPendingMetadata(void) {
    return (g_boot_app.boot_info.pending_size != 0U) && (g_boot_app.boot_info.pending_crc != 0U);
}

static bool Boot_App_IsSlotAValidWithMetadata(uint32_t size, uint32_t crc32) {
    return (size != 0U) && (crc32 != 0U) &&
           (Boot_SimpleJump_IsSlotValidWithMetadata(SLOT_A, size, crc32) != false);
}

static bool Boot_App_IsCurrentSlotAValid(void) {
    if (Boot_App_HasSlotMetadata(SLOT_A) != false) {
        return Boot_App_IsSlotAValidWithMetadata(Boot_App_GetSlotExpectedSize(SLOT_A),
                                                 Boot_App_GetSlotExpectedCrc(SLOT_A));
    }

    return Boot_SimpleJump_IsSlotValid(SLOT_A);
}

static bool Boot_App_IsConfirmedSlotAValid(void) {
    return Boot_App_HasSlotMetadata(SLOT_A) &&
           Boot_App_IsSlotAValidWithMetadata(Boot_App_GetSlotExpectedSize(SLOT_A),
                                             Boot_App_GetSlotExpectedCrc(SLOT_A));
}

static bool Boot_App_IsPendingSlotAValid(void) {
    return Boot_App_HasPendingMetadata() &&
           Boot_App_IsSlotAValidWithMetadata(g_boot_app.boot_info.pending_size,
                                             g_boot_app.boot_info.pending_crc);
}

static bool Boot_App_IsBackupSlotBValid(void) {
    return Boot_App_HasSlotMetadata(SLOT_B) &&
           (Boot_SimpleUpgrade_VerifySlotData(SLOT_B, Boot_App_GetSlotExpectedSize(SLOT_B),
                                              Boot_App_GetSlotExpectedCrc(SLOT_B)) ==
            BOOT_ERR_NONE);
}

static uint8_t Boot_App_SlotFromBase(uint32_t app_base) {
    const BootSimpleFlashRegion *slot_region;

    slot_region = Boot_SimpleFlash_GetSlotRegion(SLOT_A);
    if ((slot_region != NULL) && (slot_region->base == app_base)) {
        return SLOT_A;
    }

    slot_region = Boot_SimpleFlash_GetSlotRegion(SLOT_B);
    if ((slot_region != NULL) && (slot_region->base == app_base)) {
        return SLOT_B;
    }

    return SLOT_NONE;
}

static BootError Boot_App_ClearPendingAndKeepCurrentA(const char *reason) {
    s_BootInfo previous_info = g_boot_app.boot_info;
    BootError error;

    g_boot_app.boot_info.active_slot     = SLOT_A;
    g_boot_app.boot_info.pending_slot    = SLOT_NONE;
    g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_IDLE;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
    Boot_App_ClearPendingMetadata();

    error = Boot_App_SaveBootInfo(reason);
    if (error != BOOT_ERR_NONE) {
        g_boot_app.boot_info = previous_info;
    }

    return error;
}

static BootError Boot_App_FinalizePendingAsActive(const char *reason) {
    s_BootInfo previous_info = g_boot_app.boot_info;
    BootError error;

    if ((g_boot_app.boot_info.pending_slot != SLOT_A) || (Boot_App_HasPendingMetadata() == false)) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    Boot_App_SetSlotMetadata(SLOT_A, g_boot_app.boot_info.pending_size, g_boot_app.boot_info.pending_crc);
    g_boot_app.boot_info.active_slot     = SLOT_A;
    g_boot_app.boot_info.pending_slot    = SLOT_NONE;
    g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_SUCCESS;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
    Boot_App_ClearPendingMetadata();

    error = Boot_App_SaveBootInfo(reason);
    if (error != BOOT_ERR_NONE) {
        g_boot_app.boot_info = previous_info;
    }

    return error;
}

static BootError Boot_App_MarkBackupReadyForInstall(const BootAppImageInfo *image) {
    s_BootInfo previous_info = g_boot_app.boot_info;
    BootError error;

    if ((image == NULL) || (Boot_App_HasSlotMetadata(SLOT_A) == false)) {
        return BOOT_ERR_CTRL_CORRUPTED;
    }

    Boot_App_SetSlotMetadata(SLOT_B, Boot_App_GetSlotExpectedSize(SLOT_A),
                             Boot_App_GetSlotExpectedCrc(SLOT_A));
    Boot_App_SetPendingMetadata(image->size, image->crc32);
    g_boot_app.boot_info.active_slot     = SLOT_A;
    g_boot_app.boot_info.pending_slot    = SLOT_A;
    g_boot_app.boot_info.confirmed       = BOOT_NOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_READY;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;

    error = Boot_App_SaveBootInfo("backup ready");
    if (error != BOOT_ERR_NONE) {
        g_boot_app.boot_info = previous_info;
    }

    return error;
}

static BootError Boot_App_MarkPendingTesting(void) {
    if ((g_boot_app.boot_info.pending_slot != SLOT_A) || (Boot_App_HasPendingMetadata() == false)) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    Boot_App_SetSlotMetadata(SLOT_A, g_boot_app.boot_info.pending_size, g_boot_app.boot_info.pending_crc);
    g_boot_app.boot_info.active_slot     = SLOT_A;
    g_boot_app.boot_info.pending_slot    = SLOT_A;
    g_boot_app.boot_info.confirmed       = BOOT_NOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_TESTING;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;

    return Boot_App_SaveBootInfo("pending testing");
}

static BootError Boot_App_BackupCurrentAToB(void) {
    BootError error;

    if (Boot_App_HasSlotMetadata(SLOT_A) == false) {
        return BOOT_ERR_CTRL_CORRUPTED;
    }

    error = Boot_SimpleJump_ValidateSlot(SLOT_A, Boot_App_GetSlotExpectedSize(SLOT_A),
                                         Boot_App_GetSlotExpectedCrc(SLOT_A));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    return Boot_SimpleUpgrade_CopySlot(SLOT_A, SLOT_B, Boot_App_GetSlotExpectedSize(SLOT_A),
                                       Boot_App_GetSlotExpectedCrc(SLOT_A));
}

static BootError Boot_App_RestoreBackupToA(uint8_t rollback_reason, const char *reason) {
    BootError error;

    if (Boot_App_IsBackupSlotBValid() == false) {
        return BOOT_ERR_ROLLBACK_FAILED;
    }

    error = Boot_SimpleUpgrade_CopySlot(SLOT_B, SLOT_A, Boot_App_GetSlotExpectedSize(SLOT_B),
                                        Boot_App_GetSlotExpectedCrc(SLOT_B));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_SimpleJump_ValidateSlot(SLOT_A, Boot_App_GetSlotExpectedSize(SLOT_B),
                                         Boot_App_GetSlotExpectedCrc(SLOT_B));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    Boot_App_SetSlotMetadata(SLOT_A, Boot_App_GetSlotExpectedSize(SLOT_B),
                             Boot_App_GetSlotExpectedCrc(SLOT_B));
    g_boot_app.boot_info.active_slot     = SLOT_A;
    g_boot_app.boot_info.pending_slot    = SLOT_NONE;
    g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_ROLLBACK;
    g_boot_app.boot_info.rollback_reason = rollback_reason;
    Boot_App_ClearPendingMetadata();
    (void)Boot_App_SaveBootInfo(reason);

    g_boot_app.allow_upgrade_scan = 1U;
    g_boot_app.jump_slot          = SLOT_A;
    LOG_WARN(BOOT_LOG_TAG, "Rollback restored backup slot=B to runtime slot=A, reason=%u",
             (unsigned int)rollback_reason);
    return BOOT_ERR_NONE;
}

static void Boot_App_TryPromoteConfirmedPending(void) {
    const BootHandoffInfo *previous_handoff;
    BootError error;
    uint8_t confirmed_slot;

    if ((g_boot_app.boot_info.pending_slot != SLOT_A) || (Boot_App_HasPendingMetadata() == false)) {
        return;
    }

    previous_handoff = Boot_Handoff_GetPrevious();
    if (previous_handoff == NULL) {
        return;
    }

    if ((previous_handoff->flags & BOOT_HANDOFF_FLAG_APP_CONFIRMED) == 0U) {
        return;
    }

    if (previous_handoff->stage != BOOT_HANDOFF_STAGE_APP_READY) {
        return;
    }

    confirmed_slot = Boot_App_SlotFromBase(previous_handoff->app_base);
    if (confirmed_slot != SLOT_A) {
        LOG_WARN(BOOT_LOG_TAG, "Ignore app-ready handoff for slot=%s while pending=A",
                 Boot_Info_SlotToString(confirmed_slot));
        return;
    }

    if (Boot_App_IsPendingSlotAValid() == false) {
        LOG_WARN(BOOT_LOG_TAG, "App-ready handoff ignored, pending slot=A no longer validates");
        return;
    }

    error = Boot_App_FinalizePendingAsActive("promote confirmed pending");
    if (error == BOOT_ERR_NONE) {
        LOG_INFO(BOOT_LOG_TAG, "Pending slot=A promoted to confirmed image");
    } else {
        LOG_WARN(BOOT_LOG_TAG, "Failed to finalize pending slot=A: %s",
                 Boot_ErrorToString(error));
    }
}

static uint8_t Boot_App_FindFirstBootableSlot(void) {
    if (Boot_SimpleJump_IsSlotValid(SLOT_A) != false) {
        return SLOT_A;
    }

    return SLOT_NONE;
}

static BootError Boot_App_LoadOrInitializeBootInfo(void) {
    BootError error;
    uint8_t bootable_slot;

    error = Boot_Info_Load(&g_boot_app.boot_info);
    if (error == BOOT_ERR_NONE) {
        if (g_boot_app.boot_info.max_boot_count == 0U) {
            g_boot_app.boot_info.max_boot_count = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
            (void)Boot_App_SaveBootInfo("normalize max_boot_count");
        }
        return BOOT_ERR_NONE;
    }

    if ((error != BOOT_ERR_CTRL_NOT_FOUND) && (error != BOOT_ERR_CTRL_CRC)) {
        return error;
    }

    Boot_Info_InitDefaults(&g_boot_app.boot_info);
    bootable_slot = Boot_App_FindFirstBootableSlot();
    if (Boot_Info_IsSlotValueValid(bootable_slot) != false) {
        g_boot_app.boot_info.active_slot = bootable_slot;
    }

    LOG_WARN(BOOT_LOG_TAG, "BootInfo recreated: %s", Boot_ErrorToString(error));
    (void)Boot_App_SaveBootInfo("initialize default");
    return BOOT_ERR_NONE;
}

static BootError Boot_App_TrySelfHealCurrentAMetadata(void) {
    const BootSimpleFlashRegion *slot_region;
    s_BootInfo previous_info;
    uint32_t slot_crc = 0U;
    BootError error;

    if (Boot_App_HasSlotMetadata(SLOT_A) != false) {
        return BOOT_ERR_NONE;
    }

    if ((g_boot_app.boot_info.pending_slot != SLOT_NONE) ||
        ((g_boot_app.boot_info.upgrade_state != UPGRADE_IDLE) &&
         (g_boot_app.boot_info.upgrade_state != UPGRADE_SUCCESS) &&
         (g_boot_app.boot_info.upgrade_state != UPGRADE_ROLLBACK))) {
        return BOOT_ERR_NONE;
    }

    if (Boot_SimpleJump_IsSlotValid(SLOT_A) == false) {
        return BOOT_ERR_NONE;
    }

    slot_region = Boot_SimpleFlash_GetSlotRegion(SLOT_A);
    if (slot_region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    error = Boot_SimpleUpgrade_ComputeSlotCrc(SLOT_A, slot_region->size, &slot_crc);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    previous_info = g_boot_app.boot_info;
    Boot_App_SetSlotMetadata(SLOT_A, slot_region->size, slot_crc);
    g_boot_app.boot_info.active_slot = SLOT_A;

    error = Boot_App_SaveBootInfo("self-heal slot=A metadata");
    if (error != BOOT_ERR_NONE) {
        g_boot_app.boot_info = previous_info;
        return error;
    }

    LOG_WARN(BOOT_LOG_TAG,
             "Slot=A metadata self-healed using full-slot digest: size=%lu crc=0x%08lX",
             (unsigned long)slot_region->size, (unsigned long)slot_crc);
    return BOOT_ERR_NONE;
}

static BootError Boot_App_HandlePendingReadyState(void) {
    BootError error;

    if ((g_boot_app.boot_info.pending_slot != SLOT_A) || (Boot_App_HasPendingMetadata() == false) ||
        (g_boot_app.boot_info.upgrade_state != UPGRADE_READY)) {
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsPendingSlotAValid() != false) {
        error = Boot_App_MarkPendingTesting();
        if (error != BOOT_ERR_NONE) {
            LOG_WARN(BOOT_LOG_TAG, "Pending slot=A installed but testing metadata save failed: %s",
                     Boot_ErrorToString(error));
        }

        g_boot_app.allow_upgrade_scan = 0U;
        g_boot_app.jump_slot          = SLOT_A;
        LOG_INFO(BOOT_LOG_TAG, "Pending slot=A selected after interrupted install");
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsConfirmedSlotAValid() != false) {
        error = Boot_App_ClearPendingAndKeepCurrentA("cancel incomplete install");
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        g_boot_app.allow_upgrade_scan = 1U;
        g_boot_app.jump_slot          = SLOT_A;
        LOG_INFO(BOOT_LOG_TAG, "Incomplete install discarded, keep current slot=A");
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsBackupSlotBValid() != false) {
        return Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "restore backup after incomplete install");
    }

    g_boot_app.allow_upgrade_scan = 1U;
    g_boot_app.jump_slot          = SLOT_NONE;
    LOG_WARN(BOOT_LOG_TAG, "Incomplete install detected, neither slot=A nor backup slot=B is bootable");
    return BOOT_ERR_NONE;
}

static BootError Boot_App_HandlePendingTestingState(void) {
    if ((g_boot_app.boot_info.pending_slot != SLOT_A) || (Boot_App_HasPendingMetadata() == false) ||
        (g_boot_app.boot_info.upgrade_state != UPGRADE_TESTING)) {
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsPendingSlotAValid() == false) {
        if (Boot_App_IsBackupSlotBValid() != false) {
            return Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "rollback invalid pending image");
        }

        g_boot_app.allow_upgrade_scan = 1U;
        g_boot_app.jump_slot          = SLOT_NONE;
        LOG_WARN(BOOT_LOG_TAG, "Pending image invalid and no rollback backup is available");
        return BOOT_ERR_NONE;
    }

    if ((Boot_App_IsWatchdogReset(g_boot_app.reset_flags) != 0) &&
        (g_boot_app.boot_info.boot_count > 0U) && (Boot_App_IsBackupSlotBValid() != false)) {
        return Boot_App_RestoreBackupToA(ROLLBACK_WDG_RESET, "rollback watchdog reset");
    }

    if ((g_boot_app.boot_info.boot_count >= g_boot_app.boot_info.max_boot_count) &&
        (Boot_App_IsBackupSlotBValid() != false)) {
        return Boot_App_RestoreBackupToA(ROLLBACK_BOOT_OVERFLOW, "rollback boot overflow");
    }

    g_boot_app.allow_upgrade_scan = 0U;
    g_boot_app.jump_slot          = SLOT_A;
    LOG_INFO(BOOT_LOG_TAG, "Pending slot=A will be tested, boot_count=%u/%u",
             (unsigned int)g_boot_app.boot_info.boot_count,
             (unsigned int)g_boot_app.boot_info.max_boot_count);
    return BOOT_ERR_NONE;
}

static BootError Boot_App_ResolveStartupPlan(void) {
    BootError error;

    error = Boot_App_LoadOrInitializeBootInfo();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    Boot_App_TryPromoteConfirmedPending();

    if (g_boot_app.boot_info.active_slot != SLOT_A) {
        g_boot_app.boot_info.active_slot = SLOT_A;
        (void)Boot_App_SaveBootInfo("normalize active slot");
    }

    error = Boot_App_TrySelfHealCurrentAMetadata();
    if (error != BOOT_ERR_NONE) {
        LOG_WARN(BOOT_LOG_TAG, "Slot=A metadata self-heal skipped: %s",
                 Boot_ErrorToString(error));
    }

    error = Boot_App_HandlePendingReadyState();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (g_boot_app.jump_slot == SLOT_A) {
        return BOOT_ERR_NONE;
    }

    error = Boot_App_HandlePendingTestingState();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (g_boot_app.jump_slot == SLOT_A) {
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsCurrentSlotAValid() != false) {
        g_boot_app.allow_upgrade_scan = 1U;
        g_boot_app.jump_slot          = SLOT_A;
        LOG_INFO(BOOT_LOG_TAG, "Stable slot=A selected");
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsBackupSlotBValid() != false) {
        return Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "restore backup for stable boot");
    }

    g_boot_app.allow_upgrade_scan = 1U;
    g_boot_app.jump_slot          = SLOT_NONE;
    LOG_WARN(BOOT_LOG_TAG, "No bootable slot=A, waiting for upgrade media");
    return BOOT_ERR_NONE;
}

static BootError Boot_App_EnsureJumpSlotValid(void) {
    if (g_boot_app.jump_slot != SLOT_A) {
        return BOOT_ERR_NO_BOOTABLE_IMAGE;
    }

    if ((g_boot_app.boot_info.pending_slot == SLOT_A) &&
        (g_boot_app.boot_info.upgrade_state == UPGRADE_TESTING)) {
        if (Boot_App_IsPendingSlotAValid() != false) {
            return BOOT_ERR_NONE;
        }

        if (Boot_App_IsBackupSlotBValid() != false) {
            return Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "restore backup before jump");
        }

        return BOOT_ERR_NO_BOOTABLE_IMAGE;
    }

    if (Boot_App_IsCurrentSlotAValid() != false) {
        return BOOT_ERR_NONE;
    }

    if (Boot_App_IsBackupSlotBValid() != false) {
        return Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "restore backup before stable jump");
    }

    return BOOT_ERR_NO_BOOTABLE_IMAGE;
}

static BootError Boot_App_PreparePendingJump(void) {
    if ((g_boot_app.boot_info.pending_slot != SLOT_A) ||
        (g_boot_app.boot_info.upgrade_state != UPGRADE_TESTING) || (g_boot_app.jump_slot != SLOT_A)) {
        return BOOT_ERR_NONE;
    }

    g_boot_app.boot_info.boot_count++;
    g_boot_app.boot_info.confirmed     = BOOT_NOT_CONFIRMED;
    g_boot_app.boot_info.upgrade_state = UPGRADE_TESTING;
    return Boot_App_SaveBootInfo("prepare pending jump");
}

void Boot_App_Init(void) {
    memset(&g_boot_app, 0, sizeof(g_boot_app));
    g_boot_app.state     = BOOT_APP_STATE_INIT;
    g_boot_app.jump_slot = SLOT_NONE;
}

void Boot_App_Process(void) {
    BootError error = BOOT_ERR_NONE;

    switch (g_boot_app.state) {
        case BOOT_APP_STATE_INIT:
            Boot_Log_Init();
            Boot_Handoff_InitBoot();
            Boot_Usb_Init();
            g_boot_app.reset_flags = Boot_Platform_ReadResetFlags();
            Boot_Platform_ClearResetFlags();
            LOG_INFO(BOOT_LOG_TAG, "A-runtime/B-backup boot init, reset_flags=0x%08lX",
                     (unsigned long)g_boot_app.reset_flags);

            error = Boot_App_ResolveStartupPlan();
            if (error != BOOT_ERR_NONE) {
                Boot_App_EnterFatal(error);
                return;
            }

            g_boot_app.state =
                (g_boot_app.allow_upgrade_scan != 0U) ? BOOT_APP_STATE_USB_SCAN : BOOT_APP_STATE_JUMP;
            return;

        case BOOT_APP_STATE_USB_SCAN: {
            BootUsbScanResult scan_result =
                Boot_Usb_PollForUpgradeMedia(BOOT_USB_WAIT_WINDOW_MS, &error);

            if (scan_result == BOOT_USB_SCAN_WAITING) {
                return;
            }

            if (scan_result == BOOT_USB_SCAN_UPGRADE_READY) {
                LOG_INFO(BOOT_LOG_TAG, "Upgrade media ready, current active=%s",
                         Boot_Info_SlotToString(g_boot_app.boot_info.active_slot));
                g_boot_app.state = BOOT_APP_STATE_IMAGE_LOAD;
                return;
            }

            if (error != BOOT_ERR_NONE) {
                LOG_INFO(BOOT_LOG_TAG, "Skip upgrade media: %s", Boot_ErrorToString(error));
            } else {
                LOG_INFO(BOOT_LOG_TAG, "No upgrade media, jump to current slot");
            }

            g_boot_app.state =
                (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
            return;
        }

        case BOOT_APP_STATE_IMAGE_LOAD:
            error = Boot_SimpleManifest_Load(&g_boot_app.image_info);
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "App package ignored: %s", Boot_ErrorToString(error));
                Boot_App_BlockRecoveryRetry(error);
                g_boot_app.state =
                    (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
                return;
            }

            LOG_INFO(BOOT_LOG_TAG,
                     "Selected app image: %s size=%lu crc=0x%08lX addr=0x%08lX project=%s git=%s",
                     g_boot_app.image_info.file, (unsigned long)g_boot_app.image_info.size,
                     (unsigned long)g_boot_app.image_info.crc32,
                     (unsigned long)g_boot_app.image_info.write_address,
                     g_boot_app.image_info.project_name, g_boot_app.image_info.git_hash);
            LOG_INFO(BOOT_LOG_TAG, "App build time: %s", g_boot_app.image_info.build_time);
            g_boot_app.state = BOOT_APP_STATE_UPGRADE;
            return;

        case BOOT_APP_STATE_UPGRADE: {
            BootError backup_error;
            BootError restore_error;

            if (Boot_App_HasSlotMetadata(SLOT_A) == false) {
                LOG_WARN(BOOT_LOG_TAG, "Upgrade aborted: slot=A metadata missing, cannot create rollback backup");
                Boot_App_BlockRecoveryRetry(BOOT_ERR_CTRL_CORRUPTED);
                g_boot_app.state =
                    (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
                return;
            }

            backup_error = Boot_App_BackupCurrentAToB();
            if (backup_error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Backup slot=A to slot=B failed: %s",
                         Boot_ErrorToString(backup_error));
                Boot_App_BlockRecoveryRetry(backup_error);
                g_boot_app.state =
                    (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
                return;
            }

            error = Boot_App_MarkBackupReadyForInstall(&g_boot_app.image_info);
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Install state save failed, keep current slot=A: %s",
                         Boot_ErrorToString(error));
                g_boot_app.state = BOOT_APP_STATE_JUMP;
                return;
            }

            error = Boot_SimpleUpgrade_Run(&g_boot_app.image_info, SLOT_A);
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Upgrade write to slot=A failed: %s",
                         Boot_ErrorToString(error));
                restore_error =
                    Boot_App_RestoreBackupToA(ROLLBACK_CRC_ERROR, "rollback after upgrade failure");
                if (restore_error != BOOT_ERR_NONE) {
                    LOG_WARN(BOOT_LOG_TAG, "Rollback restore from slot=B failed: %s",
                             Boot_ErrorToString(restore_error));
                    g_boot_app.last_error = restore_error;
                    Boot_Handoff_SetError((uint32_t)restore_error);
                    Boot_App_BlockRecoveryRetry(restore_error);
                    g_boot_app.state =
                        (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
                    return;
                }

                Boot_App_ClearRecoveryRetryBlock("rollback restored");
                g_boot_app.state = BOOT_APP_STATE_JUMP;
                return;
            }

            Boot_App_ClearRecoveryRetryBlock("upgrade success");
            error = Boot_App_MarkPendingTesting();
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Pending test metadata save failed, fallback state remains recoverable: %s",
                         Boot_ErrorToString(error));
            }

            g_boot_app.allow_upgrade_scan = 0U;
            g_boot_app.jump_slot          = SLOT_A;
            LOG_INFO(BOOT_LOG_TAG, "Upgrade written to slot=A, rollback backup preserved in slot=B");
            g_boot_app.state = BOOT_APP_STATE_JUMP;
            return;
        }

        case BOOT_APP_STATE_JUMP:
            if (Boot_Usb_IsMounted()) {
                Boot_Platform_UsbUnmount();
            }

            error = Boot_App_EnsureJumpSlotValid();
            if (error != BOOT_ERR_NONE) {
                if (error == BOOT_ERR_NO_BOOTABLE_IMAGE) {
                    LOG_WARN(BOOT_LOG_TAG, "No bootable slot, enter recovery mode");
                    g_boot_app.state = BOOT_APP_STATE_RECOVERY;
                    return;
                }

                Boot_App_EnterFatal(error);
                return;
            }

            error = Boot_App_PreparePendingJump();
            if (error != BOOT_ERR_NONE) {
                Boot_App_EnterFatal(error);
                return;
            }

            error = Boot_SimpleJump_ToSlot(g_boot_app.jump_slot);
            Boot_App_EnterFatal(error);
            return;

        case BOOT_APP_STATE_RECOVERY: {
            BootUsbScanResult scan_result =
                Boot_Usb_PollForUpgradeMedia(0xFFFFFFFFUL, &error);

            if (scan_result == BOOT_USB_SCAN_NO_DEVICE) {
                Boot_App_ClearRecoveryRetryBlock("media removed");
                return;
            }

            if (g_boot_app.recovery_retry_blocked != 0U) {
                return;
            }

            if (scan_result == BOOT_USB_SCAN_UPGRADE_READY) {
                LOG_INFO(BOOT_LOG_TAG, "Recovery media ready, try reload app image");
                g_boot_app.state = BOOT_APP_STATE_IMAGE_LOAD;
            }

            return;
        }

        case BOOT_APP_STATE_FATAL:
            Boot_App_FeedWatchdogForever();
            return;

        default:
            Boot_App_EnterFatal(BOOT_ERR_INVALID_ARGUMENT);
            return;
    }
}
