#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_HANDOFF_MAGIC   0x42484446UL
#define BOOT_HANDOFF_VERSION 0x00010000UL

#define BOOT_HANDOFF_FLAG_VECTOR_VALID  (1UL << 0U)
#define BOOT_HANDOFF_FLAG_APP_CONFIRMED (1UL << 1U)

typedef enum {
    BOOT_HANDOFF_STAGE_EMPTY = 0U,
    BOOT_HANDOFF_STAGE_BOOT_START,
    BOOT_HANDOFF_STAGE_UPGRADE_VERIFIED,
    BOOT_HANDOFF_STAGE_JUMP_READY,
    BOOT_HANDOFF_STAGE_JUMPING,
    BOOT_HANDOFF_STAGE_APP_EARLY,
    BOOT_HANDOFF_STAGE_APP_MAIN,
    BOOT_HANDOFF_STAGE_APP_READY,
    BOOT_HANDOFF_STAGE_BOOT_FAULT,
    BOOT_HANDOFF_STAGE_APP_FAULT
} BootHandoffStage;

typedef enum {
    BOOT_HANDOFF_FAULT_NONE = 0U,
    BOOT_HANDOFF_FAULT_NMI,
    BOOT_HANDOFF_FAULT_HARDFAULT,
    BOOT_HANDOFF_FAULT_MEMMANAGE,
    BOOT_HANDOFF_FAULT_BUSFAULT,
    BOOT_HANDOFF_FAULT_USAGEFAULT
} BootHandoffFault;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t session;
    uint32_t stage;
    uint32_t flags;
    uint32_t boot_tick_ms;
    uint32_t app_tick_ms;
    uint32_t app_base;
    uint32_t app_stack_pointer;
    uint32_t app_reset_handler;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t source_crc32;
    uint32_t flash_crc32;
    uint32_t last_error;
    uint32_t fault_type;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_bfar;
    uint32_t fault_mmfar;
    uint32_t fault_lr;
    uint32_t fault_sp;
} BootHandoffInfo;

const BootHandoffInfo *Boot_Handoff_Get(void);
const BootHandoffInfo *Boot_Handoff_GetPrevious(void);
const char *Boot_Handoff_StageToString(uint32_t stage);
const char *Boot_Handoff_FaultToString(uint32_t fault_type);

void Boot_Handoff_InitBoot(void);
void Boot_Handoff_RecordUpgrade(uint32_t image_size,
                                uint32_t image_crc32,
                                uint32_t source_crc32,
                                uint32_t flash_crc32);
void Boot_Handoff_RecordVector(uint32_t app_base,
                               uint32_t stack_pointer,
                               uint32_t reset_handler,
                               bool is_valid);
void Boot_Handoff_SetStage(BootHandoffStage stage);
void Boot_Handoff_SetError(uint32_t error_code);
void Boot_Handoff_RecordFault(BootHandoffFault fault_type);
void Boot_Handoff_LogCurrent(const char *label);

/*
 * App-side integration:
 * Call Boot_Handoff_AppMarkAlive(BOOT_HANDOFF_STAGE_APP_EARLY) as early as possible
 * after reset, then Boot_Handoff_AppMarkAlive(BOOT_HANDOFF_STAGE_APP_MAIN) at main().
 * Once the new image is proven stable, call Boot_Handoff_AppMarkAlive(BOOT_HANDOFF_STAGE_APP_READY)
 * and/or Boot_Info_ConfirmRunningImage() to promote the pending slot.
 */
void Boot_Handoff_AppMarkAlive(BootHandoffStage stage);

#endif
