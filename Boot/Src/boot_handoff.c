#include "boot_handoff.h"

#include <string.h>

#include "boot_config.h"
#include "boot_log.h"
#include "stm32h7xx_hal.h"

static BootHandoffInfo g_boot_handoff
    __attribute__((section(".boot_keep.handoff"), aligned(32), used));

static void Boot_Handoff_FlushCache(void) {
#if defined(SCB_CCR_DC_Msk)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache();
        __DSB();
        __ISB();
    }
#endif
}

static bool Boot_Handoff_IsValid(const BootHandoffInfo *info) {
    return (info != NULL) && (info->magic == BOOT_HANDOFF_MAGIC) &&
           (info->version == BOOT_HANDOFF_VERSION);
}

static void Boot_Handoff_LogSnapshot(const char *label, const BootHandoffInfo *info) {
    if ((label == NULL) || (info == NULL)) {
        return;
    }

    LOG_INFO(BOOT_LOG_TAG, "%s seq=%lu stage=%s flags=0x%08lX", label,
             (unsigned long) info->session, Boot_Handoff_StageToString(info->stage),
             (unsigned long) info->flags);
    LOG_INFO(BOOT_LOG_TAG, "mailbox=0x%08lX app=0x%08lX sp=0x%08lX reset=0x%08lX",
             (unsigned long) (uintptr_t) info, (unsigned long) info->app_base,
             (unsigned long) info->app_stack_pointer, (unsigned long) info->app_reset_handler);

    if ((info->image_size != 0U) || (info->image_crc32 != 0U)) {
        LOG_INFO(BOOT_LOG_TAG, "image size=%lu crc=0x%08lX src=0x%08lX flash=0x%08lX",
                 (unsigned long) info->image_size, (unsigned long) info->image_crc32,
                 (unsigned long) info->source_crc32, (unsigned long) info->flash_crc32);
    }

    if (info->last_error != 0U) {
        LOG_WARN(BOOT_LOG_TAG, "handoff error=0x%08lX", (unsigned long) info->last_error);
    }

    if (info->fault_type != BOOT_HANDOFF_FAULT_NONE) {
        LOG_WARN(BOOT_LOG_TAG, "handoff fault=%s cfsr=0x%08lX hfsr=0x%08lX",
                 Boot_Handoff_FaultToString(info->fault_type), (unsigned long) info->fault_cfsr,
                 (unsigned long) info->fault_hfsr);
        LOG_WARN(BOOT_LOG_TAG, "fault mmfar=0x%08lX bfar=0x%08lX lr=0x%08lX sp=0x%08lX",
                 (unsigned long) info->fault_mmfar, (unsigned long) info->fault_bfar,
                 (unsigned long) info->fault_lr, (unsigned long) info->fault_sp);
    }
}

const BootHandoffInfo *Boot_Handoff_Get(void) {
    return &g_boot_handoff;
}

const char *Boot_Handoff_StageToString(uint32_t stage) {
    switch (stage) {
        case BOOT_HANDOFF_STAGE_EMPTY:
            return "empty";
        case BOOT_HANDOFF_STAGE_BOOT_START:
            return "boot_start";
        case BOOT_HANDOFF_STAGE_UPGRADE_VERIFIED:
            return "upgrade_verified";
        case BOOT_HANDOFF_STAGE_JUMP_READY:
            return "jump_ready";
        case BOOT_HANDOFF_STAGE_JUMPING:
            return "jumping";
        case BOOT_HANDOFF_STAGE_APP_EARLY:
            return "app_early";
        case BOOT_HANDOFF_STAGE_APP_MAIN:
            return "app_main";
        case BOOT_HANDOFF_STAGE_APP_READY:
            return "app_ready";
        case BOOT_HANDOFF_STAGE_BOOT_FAULT:
            return "boot_fault";
        case BOOT_HANDOFF_STAGE_APP_FAULT:
            return "app_fault";
        default:
            return "unknown";
    }
}

const char *Boot_Handoff_FaultToString(uint32_t fault_type) {
    switch (fault_type) {
        case BOOT_HANDOFF_FAULT_NONE:
            return "none";
        case BOOT_HANDOFF_FAULT_NMI:
            return "nmi";
        case BOOT_HANDOFF_FAULT_HARDFAULT:
            return "hardfault";
        case BOOT_HANDOFF_FAULT_MEMMANAGE:
            return "memmanage";
        case BOOT_HANDOFF_FAULT_BUSFAULT:
            return "busfault";
        case BOOT_HANDOFF_FAULT_USAGEFAULT:
            return "usagefault";
        default:
            return "unknown";
    }
}

void Boot_Handoff_InitBoot(void) {
    BootHandoffInfo previous;
    uint32_t next_session = 1U;

    memcpy(&previous, &g_boot_handoff, sizeof(previous));
    if (Boot_Handoff_IsValid(&previous)) {
        next_session = previous.session + 1U;

        if ((previous.flags & BOOT_HANDOFF_FLAG_APP_CONFIRMED) != 0U) {
            LOG_INFO(BOOT_LOG_TAG, "Previous app handoff was confirmed");
        } else if ((previous.stage == BOOT_HANDOFF_STAGE_JUMP_READY) ||
                   (previous.stage == BOOT_HANDOFF_STAGE_JUMPING)) {
            LOG_WARN(BOOT_LOG_TAG, "Previous app handoff was not confirmed");
        } else if (previous.stage == BOOT_HANDOFF_STAGE_BOOT_FAULT) {
            LOG_WARN(BOOT_LOG_TAG, "Previous boot fault was recorded");
        }

        Boot_Handoff_LogSnapshot("Prev handoff", &previous);
    } else {
        LOG_INFO(BOOT_LOG_TAG, "Diag mailbox cold start @0x%08lX",
                 (unsigned long) (uintptr_t) &g_boot_handoff);
    }

    memset(&g_boot_handoff, 0, sizeof(g_boot_handoff));
    g_boot_handoff.magic       = BOOT_HANDOFF_MAGIC;
    g_boot_handoff.version     = BOOT_HANDOFF_VERSION;
    g_boot_handoff.session     = next_session;
    g_boot_handoff.stage       = BOOT_HANDOFF_STAGE_BOOT_START;
    g_boot_handoff.boot_tick_ms = HAL_GetTick();
    g_boot_handoff.app_base    = BOOT_APP_BASE;
    Boot_Handoff_FlushCache();

    Boot_Handoff_LogSnapshot("Boot handoff", &g_boot_handoff);
}

void Boot_Handoff_RecordUpgrade(uint32_t image_size,
                                uint32_t image_crc32,
                                uint32_t source_crc32,
                                uint32_t flash_crc32) {
    g_boot_handoff.image_size    = image_size;
    g_boot_handoff.image_crc32   = image_crc32;
    g_boot_handoff.source_crc32  = source_crc32;
    g_boot_handoff.flash_crc32   = flash_crc32;
    g_boot_handoff.boot_tick_ms  = HAL_GetTick();
    g_boot_handoff.stage         = BOOT_HANDOFF_STAGE_UPGRADE_VERIFIED;
    Boot_Handoff_FlushCache();
}

void Boot_Handoff_RecordVector(uint32_t app_base,
                               uint32_t stack_pointer,
                               uint32_t reset_handler,
                               bool is_valid) {
    g_boot_handoff.app_base          = app_base;
    g_boot_handoff.app_stack_pointer = stack_pointer;
    g_boot_handoff.app_reset_handler = reset_handler;

    if (is_valid) {
        g_boot_handoff.flags |= BOOT_HANDOFF_FLAG_VECTOR_VALID;
    } else {
        g_boot_handoff.flags &= ~BOOT_HANDOFF_FLAG_VECTOR_VALID;
    }

    Boot_Handoff_FlushCache();
}

void Boot_Handoff_SetStage(BootHandoffStage stage) {
    g_boot_handoff.stage        = (uint32_t) stage;
    g_boot_handoff.boot_tick_ms = HAL_GetTick();
    Boot_Handoff_FlushCache();
}

void Boot_Handoff_SetError(uint32_t error_code) {
    g_boot_handoff.last_error = error_code;
    Boot_Handoff_FlushCache();
}

void Boot_Handoff_RecordFault(BootHandoffFault fault_type) {
    uint32_t link_register;

    __asm volatile("mov %0, lr" : "=r"(link_register));

    g_boot_handoff.stage       = BOOT_HANDOFF_STAGE_BOOT_FAULT;
    g_boot_handoff.fault_type  = (uint32_t) fault_type;
    g_boot_handoff.boot_tick_ms = HAL_GetTick();
    g_boot_handoff.fault_cfsr  = SCB->CFSR;
    g_boot_handoff.fault_hfsr  = SCB->HFSR;
    g_boot_handoff.fault_bfar  = SCB->BFAR;
    g_boot_handoff.fault_mmfar = SCB->MMFAR;
    g_boot_handoff.fault_lr    = link_register;
    g_boot_handoff.fault_sp    = __get_MSP();
    Boot_Handoff_FlushCache();
}

void Boot_Handoff_LogCurrent(const char *label) {
    Boot_Handoff_LogSnapshot(label, &g_boot_handoff);
}

void Boot_Handoff_AppMarkAlive(BootHandoffStage stage) {
    g_boot_handoff.flags |= BOOT_HANDOFF_FLAG_APP_CONFIRMED;
    g_boot_handoff.stage       = (uint32_t) stage;
    g_boot_handoff.app_tick_ms = HAL_GetTick();
    Boot_Handoff_FlushCache();
}
