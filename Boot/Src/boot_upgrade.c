#include "boot_upgrade.h"

#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_extflash.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_log.h"
#include "boot_usb.h"

#define BOOT_UPGRADE_IO_BUFFER_SIZE    1024U
#define BOOT_CTRL_FLAG_REQUEST_VERIFY  (1UL << 0)
#define BOOT_CTRL_FLAG_REQUEST_UPGRADE (1UL << 1)
#define BOOT_CTRL_FLAG_FORCE_UPGRADE   (1UL << 2)

typedef struct
{
  uint8_t has_written_app;
  BootSlot written_slot;
} BootUpgradeExecutionResult;

static BootSlot Boot_Upgrade_ResolveTargetSlot(const BootControlBlock *ctrl, BootManifestTargetSlot target_slot);

static BootError Boot_Upgrade_BuildOperationFilePath(const char *operation_file, char *path, size_t path_size)
{
  int length;
  char relative_path[BOOT_FILE_PATH_LENGTH];

  if ((operation_file == NULL) || (path == NULL) || (path_size == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (operation_file[0] == '\0')
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  if (operation_file[0] == '/')
  {
    return Boot_Usb_BuildPath(operation_file, path, path_size);
  }

  length = snprintf(relative_path, sizeof(relative_path), "%s/%s", BOOT_USB_BOOT_DIR, operation_file);
  if ((length <= 0) || ((size_t)length >= sizeof(relative_path)))
  {
    return BOOT_ERR_FILE_SIZE;
  }

  return Boot_Usb_BuildPath(relative_path, path, path_size);
}

static BootError Boot_Upgrade_SetBootFlag(BootControlBlock *ctrl, BootManifestBootFlag flag, uint32_t value)
{
  uint32_t mask;

  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  switch (flag)
  {
    case BOOT_MANIFEST_BOOT_FLAG_REQUEST_VERIFY:
      mask = BOOT_CTRL_FLAG_REQUEST_VERIFY;
      break;

    case BOOT_MANIFEST_BOOT_FLAG_REQUEST_UPGRADE:
      mask = BOOT_CTRL_FLAG_REQUEST_UPGRADE;
      break;

    case BOOT_MANIFEST_BOOT_FLAG_FORCE_UPGRADE:
      mask = BOOT_CTRL_FLAG_FORCE_UPGRADE;
      break;

    default:
      return BOOT_ERR_MANIFEST_PARSE;
  }

  if (value != 0U)
  {
    ctrl->boot_flags |= mask;
  }
  else
  {
    ctrl->boot_flags &= ~mask;
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Upgrade_ExecuteWriteApp(const BootControlBlock *ctrl,
                                              const BootManifestWriteApp *operation,
                                              BootSlot *written_slot)
{
  BootSlot target_slot;
  const BootPartition *partition;
  BootError error;
  FIL image_file;
  FRESULT fatfs_result;
  UINT bytes_read;
  uint32_t file_size;
  uint32_t remaining;
  uint32_t write_offset;
  uint32_t source_crc32;
  uint32_t verify_crc32;
  uint32_t target_address;
  uint8_t io_buffer[BOOT_UPGRADE_IO_BUFFER_SIZE];
  char path_buffer[64];
  char log_message[BOOT_LOG_MESSAGE_MAX_LENGTH];

  if ((ctrl == NULL) || (operation == NULL) || (written_slot == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  target_slot = Boot_Upgrade_ResolveTargetSlot(ctrl, operation->target_slot);
  if (target_slot == BOOT_SLOT_NONE)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  partition = Boot_Flash_GetPartition(target_slot);
  if (partition == NULL)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  if ((operation->size == 0U) ||
      (operation->offset > partition->size) ||
      (operation->size > (partition->size - operation->offset)))
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  error = Boot_Upgrade_BuildOperationFilePath(operation->file, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  fatfs_result = f_open(&image_file, path_buffer, FA_READ);
  if (fatfs_result != FR_OK)
  {
    return BOOT_ERR_FILE_MISSING;
  }

  file_size = (uint32_t)f_size(&image_file);
  if (file_size < operation->size)
  {
    (void)f_close(&image_file);
    return BOOT_ERR_FILE_SIZE;
  }

  target_address = partition->base_address + operation->offset;
  error = Boot_Flash_Erase(target_address, operation->size);
  if (error != BOOT_ERR_NONE)
  {
    (void)f_close(&image_file);
    return error;
  }

  (void)snprintf(log_message,
                 sizeof(log_message),
                 "write_app begin: slot=%lu size=%lu",
                 (unsigned long)target_slot,
                 (unsigned long)operation->size);
  Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, log_message);

  source_crc32 = 0xFFFFFFFFUL;
  remaining = operation->size;
  write_offset = 0U;

  while (remaining > 0U)
  {
    uint32_t chunk_size = (remaining > sizeof(io_buffer)) ? (uint32_t)sizeof(io_buffer) : remaining;

    bytes_read = 0U;
    fatfs_result = f_read(&image_file, io_buffer, chunk_size, &bytes_read);
    if ((fatfs_result != FR_OK) || (bytes_read != chunk_size))
    {
      (void)f_close(&image_file);
      return BOOT_ERR_FILE_SIZE;
    }

    source_crc32 = Boot_Crc32_Calc(io_buffer, chunk_size, source_crc32);
    error = Boot_Flash_Write(target_address + write_offset, io_buffer, chunk_size);
    if (error != BOOT_ERR_NONE)
    {
      (void)f_close(&image_file);
      return error;
    }

    write_offset += chunk_size;
    remaining -= chunk_size;
  }

  (void)f_close(&image_file);

  if (source_crc32 != operation->crc32)
  {
    return BOOT_ERR_FILE_CRC;
  }

  verify_crc32 = Boot_Crc32_Calc((const void *)target_address, operation->size, 0xFFFFFFFFUL);
  if (verify_crc32 != operation->crc32)
  {
    return BOOT_ERR_FLASH_VERIFY;
  }

  (void)snprintf(log_message,
                 sizeof(log_message),
                 "write_app done: slot=%lu crc=0x%08lx",
                 (unsigned long)target_slot,
                 (unsigned long)verify_crc32);
  Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, log_message);
  *written_slot = target_slot;
  return BOOT_ERR_NONE;
}

static BootError Boot_Upgrade_ExecuteOperation(BootControlBlock *ctrl,
                                               const BootManifestOperation *operation,
                                               BootUpgradeExecutionResult *result)
{
  BootError error;

  if ((ctrl == NULL) || (operation == NULL) || (result == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  switch (operation->type)
  {
    case BOOT_OP_WRITE_APP:
      error = Boot_Upgrade_ExecuteWriteApp(ctrl, &operation->payload.write_app, &result->written_slot);
      if (error != BOOT_ERR_NONE)
      {
        return error;
      }
      result->has_written_app = 1U;
      return BOOT_ERR_NONE;

    case BOOT_OP_SET_BOOT_FLAG:
      return Boot_Upgrade_SetBootFlag(ctrl,
                                      operation->payload.set_boot_flag.flag,
                                      operation->payload.set_boot_flag.value);

    case BOOT_OP_ERASE_EXTFLASH:
      return Boot_ExtFlash_Erase(operation->payload.erase_extflash.offset,
                                 operation->payload.erase_extflash.size);

    case BOOT_OP_WRITE_EXTFLASH:
      return BOOT_ERR_NOT_IMPLEMENTED;

    case BOOT_OP_DUMP_EXTFLASH:
      return BOOT_ERR_NOT_IMPLEMENTED;

    case BOOT_OP_WRITE_CONFIG:
      return BOOT_ERR_NOT_IMPLEMENTED;

    case BOOT_OP_INVALID:
    default:
      return BOOT_ERR_MANIFEST_PARSE;
  }
}

static BootSlot Boot_Upgrade_ResolveTargetSlot(const BootControlBlock *ctrl, BootManifestTargetSlot target_slot)
{
  if (ctrl == NULL)
  {
    return BOOT_SLOT_NONE;
  }

  switch (target_slot)
  {
    case BOOT_MANIFEST_SLOT_APP1:
      return BOOT_SLOT_APP1;

    case BOOT_MANIFEST_SLOT_APP2:
      return BOOT_SLOT_APP2;

    case BOOT_MANIFEST_SLOT_INACTIVE:
      return Boot_Ctrl_GetInactiveSlot(ctrl);

    case BOOT_MANIFEST_SLOT_NONE:
    default:
      return BOOT_SLOT_NONE;
  }
}

BootError Boot_Upgrade_BuildPlan(const BootControlBlock *ctrl, const BootManifest *manifest, BootUpgradePlan *plan)
{
  BootSlot resolved_slot;

  if ((ctrl == NULL) || (manifest == NULL) || (plan == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(plan, 0, sizeof(*plan));
  plan->target_slot = Boot_Ctrl_GetInactiveSlot(ctrl);

  for (uint32_t i = 0U; i < manifest->operation_count; ++i)
  {
    switch (manifest->operations[i].type)
    {
      case BOOT_OP_WRITE_APP:
        plan->has_app_update = 1U;
        resolved_slot = Boot_Upgrade_ResolveTargetSlot(ctrl, manifest->operations[i].payload.write_app.target_slot);
        if (resolved_slot != BOOT_SLOT_NONE)
        {
          plan->target_slot = resolved_slot;
        }
        break;

      case BOOT_OP_WRITE_CONFIG:
      case BOOT_OP_SET_BOOT_FLAG:
        plan->has_config_update = 1U;
        break;

      case BOOT_OP_WRITE_EXTFLASH:
      case BOOT_OP_ERASE_EXTFLASH:
      case BOOT_OP_DUMP_EXTFLASH:
        plan->has_extflash_update = 1U;
        break;

      default:
        break;
    }
  }

  if ((plan->has_app_update == 0U) &&
      (plan->has_config_update == 0U) &&
      (plan->has_extflash_update == 0U))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Upgrade_Execute(BootControlBlock *ctrl, const BootManifest *manifest, const BootUpgradePlan *plan)
{
  BootUpgradeExecutionResult execution_result;
  BootImageRecord record;
  BootError error;
  char log_message[BOOT_LOG_MESSAGE_MAX_LENGTH];

  if ((ctrl == NULL) || (manifest == NULL) || (plan == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(&execution_result, 0, sizeof(execution_result));
  memset(&record, 0, sizeof(record));

  for (uint32_t i = 0U; i < manifest->operation_count; ++i)
  {
    error = Boot_Upgrade_ExecuteOperation(ctrl, &manifest->operations[i], &execution_result);
    if (error != BOOT_ERR_NONE)
    {
      (void)snprintf(log_message,
                     sizeof(log_message),
                     "upgrade op%lu failed: type=%lu",
                     (unsigned long)i,
                     (unsigned long)manifest->operations[i].type);
      Boot_Log_Write(BOOT_LOG_ERROR, error, log_message);
      return error;
    }
  }

  if (execution_result.has_written_app != 0U)
  {
    error = Boot_Image_ValidateSlot(execution_result.written_slot, &record);
    if (error != BOOT_ERR_NONE)
    {
      Boot_Log_Write(BOOT_LOG_ERROR, error, "new image validation failed");
      return error;
    }

    error = Boot_Ctrl_PreparePendingSlot(ctrl, execution_result.written_slot, &record);
    if (error != BOOT_ERR_NONE)
    {
      return error;
    }

    if (manifest->policy.max_boot_attempts > 0U)
    {
      ctrl->max_boot_attempts = manifest->policy.max_boot_attempts;
    }

    error = Boot_Ctrl_Save(ctrl);
    if (error != BOOT_ERR_NONE)
    {
      return error;
    }

    (void)snprintf(log_message,
                   sizeof(log_message),
                   "pending slot prepared: %lu",
                   (unsigned long)execution_result.written_slot);
    Boot_Log_Write(BOOT_LOG_INFO, BOOT_ERR_NONE, log_message);
  }

  return BOOT_ERR_NONE;
}
