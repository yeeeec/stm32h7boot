#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_error.h"

/*==========================================================
 * BootInfo fixed marker
 *==========================================================*/
#define BOOT_INFO_MAGIC     0x5A5AA5A5u

/*==========================================================
 * Slot definition
 *==========================================================*/
#define SLOT_A              0u
#define SLOT_B              1u
#define SLOT_NONE           0xFFu

/*==========================================================
 * Confirm state
 *==========================================================*/
#define BOOT_NOT_CONFIRMED  0u
#define BOOT_CONFIRMED      1u

/*==========================================================
 * Upgrade state
 *==========================================================*/
#define UPGRADE_IDLE        0u
#define UPGRADE_READY       1u
#define UPGRADE_TESTING     2u
#define UPGRADE_ROLLBACK    3u
#define UPGRADE_SUCCESS     4u

/*==========================================================
 * Rollback reason
 *==========================================================*/
#define ROLLBACK_NONE           0u
#define ROLLBACK_CRC_ERROR      1u
#define ROLLBACK_BOOT_TIMEOUT   2u
#define ROLLBACK_BOOT_OVERFLOW  3u
#define ROLLBACK_WDG_RESET      4u
#define ROLLBACK_APP_FAULT      5u

typedef struct
{
    uint32_t magic;
    uint8_t  active_slot;
    uint8_t  pending_slot;
    uint8_t  confirmed;
    uint8_t  boot_count;
    uint8_t  max_boot_count;
    uint8_t  upgrade_state;
    uint8_t  rollback_reason;
    uint8_t  reserved0;
    uint32_t version_a;
    uint32_t version_b;
    uint32_t app_a_size;
    uint32_t app_b_size;
    uint32_t app_a_crc;
    uint32_t app_b_crc;
    uint32_t last_reset_reason;
    uint32_t seq;
    uint32_t crc;
} s_BootInfo;

uint32_t Boot_Info_CalcStructCrc(const s_BootInfo *info);
bool Boot_Info_IsStructValid(const s_BootInfo *info);
bool Boot_Info_IsSlotValueValid(uint8_t slot);
const char *Boot_Info_SlotToString(uint8_t slot);
void Boot_Info_InitDefaults(s_BootInfo *info);
BootError Boot_Info_Load(s_BootInfo *info);
BootError Boot_Info_Store(s_BootInfo *info);
uint8_t Boot_Info_DetectRunningSlot(void);
BootError Boot_Info_ConfirmRunningImage(void);

#endif
