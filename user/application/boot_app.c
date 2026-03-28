#include "boot_app.h"

#include <string.h>

#include "boot_handoff.h"
#include "boot_info.h"
#include "boot_log.h"
#include "platform/boot_platform.h"
#include "boot_simple_flash.h"
#include "boot_simple_jump.h"
#include "boot_simple_manifest.h"
#include "boot_simple_upgrade.h"
#include "boot_usb.h"
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
    Boot_Handoff_SetError((uint32_t) error);
    Boot_Handoff_LogCurrent("Fatal handoff");
    LOG_ERROR(BOOT_LOG_TAG, "Fatal boot error: %s", Boot_ErrorToString(error));
    Boot_App_FeedWatchdogForever();
}

static uint8_t Boot_App_GetOtherSlot(uint8_t slot) {
    return (slot == SLOT_A) ? SLOT_B : SLOT_A;
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
                 (unsigned int) g_boot_app.boot_info.confirmed,
                 (unsigned int) g_boot_app.boot_info.boot_count,
                 (unsigned int) g_boot_app.boot_info.upgrade_state);
    }

    return error;
}

static void Boot_App_SetSlotMetadata(uint8_t slot, uint32_t crc32) {
    if (slot == SLOT_A) {
        g_boot_app.boot_info.version_a = 0U;
        g_boot_app.boot_info.app_a_crc = crc32;
    } else if (slot == SLOT_B) {
        g_boot_app.boot_info.version_b = 0U;
        g_boot_app.boot_info.app_b_crc = crc32;
    }
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

static BootError Boot_App_FinalizePendingAsActive(const char *reason) {
    uint8_t promoted_slot = g_boot_app.boot_info.pending_slot;

    if (Boot_Info_IsSlotValueValid(promoted_slot) == false) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    g_boot_app.boot_info.active_slot     = promoted_slot;
    g_boot_app.boot_info.pending_slot    = SLOT_NONE;
    g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_SUCCESS;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
    return Boot_App_SaveBootInfo(reason);
}

static void Boot_App_TryPromoteConfirmedPending(void) {
    const BootHandoffInfo *previous_handoff;
    BootError error;
    uint8_t confirmed_slot;

    if (g_boot_app.boot_info.pending_slot == SLOT_NONE) {
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
    if (confirmed_slot != g_boot_app.boot_info.pending_slot) {
        LOG_WARN(BOOT_LOG_TAG,
                 "Ignore app-ready handoff for slot=%s while pending=%s",
                 Boot_Info_SlotToString(confirmed_slot),
                 Boot_Info_SlotToString(g_boot_app.boot_info.pending_slot));
        return;
    }

    if (Boot_SimpleJump_IsSlotValidWithCrc(
            confirmed_slot, Boot_App_GetSlotExpectedCrc(confirmed_slot)) == false) {
        LOG_WARN(BOOT_LOG_TAG, "App-ready handoff ignored, slot=%s no longer validates",
                 Boot_Info_SlotToString(confirmed_slot));
        return;
    }

    error = Boot_App_FinalizePendingAsActive("promote confirmed pending");
    if (error == BOOT_ERR_NONE) {
        LOG_INFO(BOOT_LOG_TAG, "Pending slot=%s promoted to active from app-ready handoff",
                 Boot_Info_SlotToString(confirmed_slot));
    } else {
        LOG_WARN(BOOT_LOG_TAG, "Failed to promote confirmed pending slot=%s: %s",
                 Boot_Info_SlotToString(confirmed_slot), Boot_ErrorToString(error));
    }
}

static uint8_t Boot_App_FindFirstBootableSlot(void) {
    if (Boot_SimpleJump_IsSlotValid(SLOT_A) != false) {
        return SLOT_A;
    }

    if (Boot_SimpleJump_IsSlotValid(SLOT_B) != false) {
        return SLOT_B;
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
            (void) Boot_App_SaveBootInfo("normalize max_boot_count");
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
    (void) Boot_App_SaveBootInfo("initialize default");
    return BOOT_ERR_NONE;
}

static BootError Boot_App_RollbackPending(uint8_t rollback_reason) {
    uint8_t fallback_slot = g_boot_app.boot_info.active_slot;

    if (Boot_SimpleJump_IsSlotValidWithCrc(fallback_slot, Boot_App_GetSlotExpectedCrc(fallback_slot)) ==
        false) {
        fallback_slot = Boot_App_GetOtherSlot(g_boot_app.boot_info.pending_slot);
        if (Boot_SimpleJump_IsSlotValidWithCrc(
                fallback_slot, Boot_App_GetSlotExpectedCrc(fallback_slot)) == false) {
            fallback_slot = SLOT_NONE;
        }
    }

    if (Boot_Info_IsSlotValueValid(fallback_slot) == false) {
        return BOOT_ERR_ROLLBACK_FAILED;
    }

    g_boot_app.boot_info.active_slot     = fallback_slot;
    g_boot_app.boot_info.pending_slot    = SLOT_NONE;
    g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_ROLLBACK;
    g_boot_app.boot_info.rollback_reason = rollback_reason;
    (void) Boot_App_SaveBootInfo("rollback pending");

    g_boot_app.allow_upgrade_scan = 0U;
    g_boot_app.jump_slot          = fallback_slot;
    LOG_WARN(BOOT_LOG_TAG, "Rollback to slot=%s reason=%u",
             Boot_Info_SlotToString(fallback_slot), (unsigned int) rollback_reason);
    return BOOT_ERR_NONE;
}

static BootError Boot_App_ResolveStartupPlan(void) {
    BootError error;
    uint8_t active_slot;
    uint8_t other_slot;
    uint8_t stable_slot;

    error = Boot_App_LoadOrInitializeBootInfo();
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    Boot_App_TryPromoteConfirmedPending();

    active_slot = g_boot_app.boot_info.active_slot;
    other_slot  = Boot_App_GetOtherSlot(active_slot);

    if (g_boot_app.boot_info.pending_slot != SLOT_NONE) {
        if (Boot_SimpleJump_IsSlotValidWithCrc(
                g_boot_app.boot_info.pending_slot,
                Boot_App_GetSlotExpectedCrc(g_boot_app.boot_info.pending_slot)) == false) {
            return Boot_App_RollbackPending(ROLLBACK_CRC_ERROR);
        }

        if ((Boot_App_IsWatchdogReset(g_boot_app.reset_flags) != 0) &&
            (g_boot_app.boot_info.boot_count > 0U)) {
            return Boot_App_RollbackPending(ROLLBACK_WDG_RESET);
        }

        if (g_boot_app.boot_info.boot_count >= g_boot_app.boot_info.max_boot_count) {
            return Boot_App_RollbackPending(ROLLBACK_BOOT_OVERFLOW);
        }

        g_boot_app.allow_upgrade_scan = 0U;
        g_boot_app.jump_slot          = g_boot_app.boot_info.pending_slot;
        LOG_INFO(BOOT_LOG_TAG, "Pending slot=%s will be tested, boot_count=%u/%u",
                 Boot_Info_SlotToString(g_boot_app.jump_slot),
                 (unsigned int) g_boot_app.boot_info.boot_count,
                 (unsigned int) g_boot_app.boot_info.max_boot_count);
        return BOOT_ERR_NONE;
    }

    stable_slot = SLOT_NONE;
    if (Boot_SimpleJump_IsSlotValidWithCrc(active_slot, Boot_App_GetSlotExpectedCrc(active_slot)) !=
        false) {
        stable_slot = active_slot;
    } else if (Boot_SimpleJump_IsSlotValidWithCrc(
                   other_slot, Boot_App_GetSlotExpectedCrc(other_slot)) != false) {
        stable_slot = other_slot;
        g_boot_app.boot_info.active_slot     = other_slot;
        g_boot_app.boot_info.pending_slot    = SLOT_NONE;
        g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
        g_boot_app.boot_info.boot_count      = 0U;
        g_boot_app.boot_info.upgrade_state   = UPGRADE_IDLE;
        g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
        (void) Boot_App_SaveBootInfo("switch active slot");
    }

    g_boot_app.allow_upgrade_scan = 1U;
    g_boot_app.jump_slot          = stable_slot;

    if (stable_slot != SLOT_NONE) {
        LOG_INFO(BOOT_LOG_TAG, "Stable slot=%s selected", Boot_Info_SlotToString(stable_slot));
    } else {
        LOG_WARN(BOOT_LOG_TAG, "No bootable slot, waiting for upgrade media");
    }

    return BOOT_ERR_NONE;
}

static void Boot_App_ArmPendingUpgrade(uint8_t target_slot, const BootAppImageInfo *image) {
    Boot_App_SetSlotMetadata(target_slot, image->crc32);
    g_boot_app.boot_info.pending_slot    = target_slot;
    g_boot_app.boot_info.confirmed       = BOOT_NOT_CONFIRMED;
    g_boot_app.boot_info.boot_count      = 0U;
    g_boot_app.boot_info.upgrade_state   = UPGRADE_READY;
    g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
}

static BootError Boot_App_EnsureJumpSlotValid(void) {
    uint8_t other_slot;

    if (Boot_Info_IsSlotValueValid(g_boot_app.jump_slot) == false) {
        return BOOT_ERR_NO_BOOTABLE_IMAGE;
    }

    if (Boot_SimpleJump_IsSlotValidWithCrc(
            g_boot_app.jump_slot, Boot_App_GetSlotExpectedCrc(g_boot_app.jump_slot)) != false) {
        return BOOT_ERR_NONE;
    }

    if ((g_boot_app.boot_info.pending_slot != SLOT_NONE) &&
        (g_boot_app.jump_slot == g_boot_app.boot_info.pending_slot)) {
        return Boot_App_RollbackPending(ROLLBACK_CRC_ERROR);
    }

    other_slot = Boot_App_GetOtherSlot(g_boot_app.jump_slot);
    if (Boot_SimpleJump_IsSlotValidWithCrc(
            other_slot, Boot_App_GetSlotExpectedCrc(other_slot)) != false) {
        g_boot_app.boot_info.active_slot     = other_slot;
        g_boot_app.boot_info.pending_slot    = SLOT_NONE;
        g_boot_app.boot_info.confirmed       = BOOT_CONFIRMED;
        g_boot_app.boot_info.boot_count      = 0U;
        g_boot_app.boot_info.upgrade_state   = UPGRADE_IDLE;
        g_boot_app.boot_info.rollback_reason = ROLLBACK_NONE;
        (void) Boot_App_SaveBootInfo("fallback jump slot");
        g_boot_app.jump_slot = other_slot;
        return BOOT_ERR_NONE;
    }

    return BOOT_ERR_NO_BOOTABLE_IMAGE;
}

static BootError Boot_App_PreparePendingJump(void) {
    if ((g_boot_app.boot_info.pending_slot == SLOT_NONE) ||
        (g_boot_app.jump_slot != g_boot_app.boot_info.pending_slot)) {
        return BOOT_ERR_NONE;
    }

    if (g_boot_app.boot_info.boot_count >= g_boot_app.boot_info.max_boot_count) {
        return BOOT_ERR_PENDING_SLOT_EXHAUSTED;
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
            LOG_INFO(BOOT_LOG_TAG, "Dual-slot boot init, reset_flags=0x%08lX",
                     (unsigned long) g_boot_app.reset_flags);

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
                g_boot_app.state = BOOT_APP_STATE_JUMP;
                return;
            }

            LOG_INFO(BOOT_LOG_TAG,
                     "Selected app image: %s size=%lu crc=0x%08lX addr=0x%08lX project=%s git=%s",
                     g_boot_app.image_info.file, (unsigned long) g_boot_app.image_info.size,
                     (unsigned long) g_boot_app.image_info.crc32,
                     (unsigned long) g_boot_app.image_info.write_address,
                     g_boot_app.image_info.project_name, g_boot_app.image_info.git_hash);
            LOG_INFO(BOOT_LOG_TAG, "App build time: %s", g_boot_app.image_info.build_time);
            g_boot_app.state = BOOT_APP_STATE_UPGRADE;
            return;

        case BOOT_APP_STATE_UPGRADE: {
            uint8_t target_slot;

            target_slot = Boot_App_GetOtherSlot(g_boot_app.boot_info.active_slot);
            if ((g_boot_app.jump_slot == SLOT_NONE) &&
                (Boot_SimpleJump_IsSlotValidWithCrc(
                     g_boot_app.boot_info.active_slot,
                     Boot_App_GetSlotExpectedCrc(g_boot_app.boot_info.active_slot)) == false)) {
                target_slot = SLOT_A;
            }

            error = Boot_SimpleUpgrade_Run(&g_boot_app.image_info, target_slot);
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Upgrade failed, keep running slot=%s: %s",
                         Boot_Info_SlotToString(g_boot_app.jump_slot),
                         Boot_ErrorToString(error));
                g_boot_app.last_error = error;
                Boot_Handoff_SetError((uint32_t) error);
                g_boot_app.state =
                    (g_boot_app.jump_slot == SLOT_NONE) ? BOOT_APP_STATE_RECOVERY : BOOT_APP_STATE_JUMP;
                return;
            }

            Boot_App_ArmPendingUpgrade(target_slot, &g_boot_app.image_info);
            error = Boot_App_SaveBootInfo("arm pending upgrade");
            if (error != BOOT_ERR_NONE) {
                LOG_WARN(BOOT_LOG_TAG, "Pending upgrade metadata not stored, stay on stable slot");
                g_boot_app.state = BOOT_APP_STATE_JUMP;
                return;
            }

            g_boot_app.jump_slot = target_slot;
            LOG_INFO(BOOT_LOG_TAG, "Upgrade armed for slot=%s", Boot_Info_SlotToString(target_slot));
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
                if (Boot_App_RollbackPending(ROLLBACK_BOOT_OVERFLOW) == BOOT_ERR_NONE) {
                    error = Boot_App_EnsureJumpSlotValid();
                    if (error == BOOT_ERR_NONE) {
                        error = Boot_SimpleJump_ToSlot(g_boot_app.jump_slot);
                    }
                }
                Boot_App_EnterFatal(error);
                return;
            }

            error = Boot_SimpleJump_ToSlot(g_boot_app.jump_slot);
            Boot_App_EnterFatal(error);
            return;

        case BOOT_APP_STATE_RECOVERY: {
            BootUsbScanResult scan_result =
                Boot_Usb_PollForUpgradeMedia(0xFFFFFFFFUL, &error);

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
