#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#include <stdint.h>

#include "boot_config.h"

typedef enum
{
  BOOT_SLOT_NONE = 0,
  BOOT_SLOT_APP1 = 1,
  BOOT_SLOT_APP2 = 2
} BootSlot;

typedef enum
{
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_UPGRADE,
  BOOT_MODE_RECOVERY,
  BOOT_MODE_FORCE_UPGRADE
} BootMode;

typedef enum
{
  BOOT_STATE_INIT = 0,
  BOOT_STATE_LOAD_CTRL,
  BOOT_STATE_USB_WAIT,
  BOOT_STATE_MANIFEST_PARSE,
  BOOT_STATE_UPGRADE_EXECUTE,
  BOOT_STATE_ARBITRATE,
  BOOT_STATE_JUMP,
  BOOT_STATE_RECOVERY
} BootState;

typedef enum
{
  BOOT_LOG_INFO = 0,
  BOOT_LOG_WARN,
  BOOT_LOG_ERROR,
  BOOT_LOG_RESULT
} BootLogLevel;

typedef enum
{
  BOOT_IMAGE_TYPE_APP = 1
} BootImageType;

typedef enum
{
  BOOT_OP_INVALID = 0,
  BOOT_OP_WRITE_APP,
  BOOT_OP_WRITE_CONFIG,
  BOOT_OP_WRITE_EXTFLASH,
  BOOT_OP_ERASE_EXTFLASH,
  BOOT_OP_DUMP_EXTFLASH,
  BOOT_OP_SET_BOOT_FLAG
} BootOperationType;

typedef enum
{
  BOOT_MANIFEST_SLOT_NONE = 0,
  BOOT_MANIFEST_SLOT_APP1,
  BOOT_MANIFEST_SLOT_APP2,
  BOOT_MANIFEST_SLOT_INACTIVE
} BootManifestTargetSlot;

typedef enum
{
  BOOT_MANIFEST_CONFIG_REGION_NONE = 0,
  BOOT_MANIFEST_CONFIG_REGION_SYS_CONFIG
} BootManifestConfigRegion;

typedef enum
{
  BOOT_MANIFEST_BOOT_FLAG_NONE = 0,
  BOOT_MANIFEST_BOOT_FLAG_REQUEST_VERIFY,
  BOOT_MANIFEST_BOOT_FLAG_REQUEST_UPGRADE,
  BOOT_MANIFEST_BOOT_FLAG_FORCE_UPGRADE
} BootManifestBootFlag;

typedef struct
{
  uint32_t base_address;
  uint32_t size;
} BootPartition;

typedef struct
{
  uint32_t version;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t flags;
  uint8_t is_valid;
  uint8_t reserved[3];
} BootImageRecord;

typedef struct
{
  uint32_t magic;
  uint16_t struct_version;
  uint16_t length;
  uint32_t sequence;
  uint8_t active_slot;
  uint8_t confirmed_slot;
  uint8_t pending_slot;
  uint8_t boot_attempts;
  uint8_t max_boot_attempts;
  uint8_t upgrade_in_progress;
  uint8_t rollback_requested;
  uint8_t reserved0;
  BootImageRecord app1;
  BootImageRecord app2;
  uint32_t last_error;
  uint32_t boot_flags;
  uint32_t crc32;
} BootControlBlock;

typedef struct
{
  uint32_t magic;
  uint16_t header_version;
  uint16_t header_size;
  uint32_t image_type;
  uint32_t target_slot;
  uint32_t image_size;
  uint32_t load_address;
  uint32_t entry_address;
  uint32_t version;
  uint32_t image_crc32;
  uint32_t flags;
  char product[BOOT_NAME_LENGTH];
  char board[BOOT_NAME_LENGTH];
  char build_time[BOOT_BUILD_TIME_LENGTH];
  uint32_t reserved[8];
} BootImageHeader;

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  BootManifestTargetSlot target_slot;
  uint32_t offset;
  uint32_t size;
  uint32_t crc32;
} BootManifestWriteApp;

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  BootManifestConfigRegion target_region;
  uint32_t size;
  uint32_t crc32;
  uint32_t preserve_count;
  char preserve[BOOT_MANIFEST_MAX_PRESERVE_ITEMS][BOOT_MANIFEST_TEXT_LENGTH];
} BootManifestWriteConfig;

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  uint32_t offset;
  uint32_t size;
  uint32_t crc32;
} BootManifestWriteExtFlash;

typedef struct
{
  uint32_t offset;
  uint32_t size;
} BootManifestEraseExtFlash;

typedef struct
{
  uint32_t offset;
  uint32_t size;
  char output[BOOT_FILE_PATH_LENGTH];
} BootManifestDumpExtFlash;

typedef struct
{
  BootManifestBootFlag flag;
  uint32_t value;
} BootManifestSetBootFlag;

typedef union
{
  BootManifestWriteApp write_app;
  BootManifestWriteConfig write_config;
  BootManifestWriteExtFlash write_extflash;
  BootManifestEraseExtFlash erase_extflash;
  BootManifestDumpExtFlash dump_extflash;
  BootManifestSetBootFlag set_boot_flag;
} BootManifestOperationPayload;

typedef struct
{
  BootOperationType type;
  BootManifestOperationPayload payload;
} BootManifestOperation;

typedef struct
{
  uint8_t allow_downgrade;
  uint8_t require_signature;
  uint8_t max_boot_attempts;
  uint8_t reserved;
} BootManifestPolicy;

typedef struct
{
  char file[BOOT_FILE_NAME_LENGTH];
  char algo[BOOT_MANIFEST_TEXT_LENGTH];
} BootManifestSignature;

typedef struct
{
  uint32_t format_version;
  char vendor[BOOT_NAME_LENGTH];
  char product[BOOT_NAME_LENGTH];
  char board[BOOT_NAME_LENGTH];
  char bundle_version[BOOT_NAME_LENGTH];
  char bundle_id[BOOT_BUNDLE_NAME_LENGTH];
  char serial_limit[BOOT_BUNDLE_NAME_LENGTH];
  BootManifestPolicy policy;
  uint32_t operation_count;
  BootManifestOperation operations[BOOT_MANIFEST_MAX_OPERATIONS];
  BootManifestSignature signature;
} BootManifest;

typedef struct
{
  BootSlot target_slot;
  uint8_t has_app_update;
  uint8_t has_config_update;
  uint8_t has_extflash_update;
  uint8_t reserved;
} BootUpgradePlan;

typedef struct
{
  uint32_t sequence;
  uint32_t timestamp_ms;
  BootLogLevel level;
  uint32_t error_code;
  char message[BOOT_LOG_MESSAGE_MAX_LENGTH];
} BootLogEntry;

typedef struct
{
  BootState state;
  BootMode mode;
  BootSlot slot_to_boot;
  uint32_t state_entry_tick;
  uint32_t last_error;
  BootControlBlock ctrl;
  BootManifest manifest;
  BootUpgradePlan plan;
} BootContext;

#endif
