/**
 * @file update_service.c
 * @brief Bounded package preparation and inactive-pair installation capability.
 */
#include "services/use_case/update_service.h"

#include <stddef.h>
#include <string.h>

#include "services/capability/checked_arithmetic.h"
#include "services/capability/slot_policy.h"
#include "logging.h"

static const char *BootPairName(boot_pair_t pair)
{
    switch (pair)
    {
        case BOOT_PAIR_NONE:
            return "none";
        case BOOT_PAIR_1:
            return "pair-1";
        case BOOT_PAIR_2:
            return "pair-2";
        default:
            return "unknown";
    }
}

static const char *UpdateStageName(update_stage_t stage)
{
    switch (stage)
    {
        case UPDATE_STAGE_IDLE:
            return "idle";
        case UPDATE_STAGE_OPEN_MANIFEST:
            return "open-manifest";
        case UPDATE_STAGE_READ_MANIFEST:
            return "read-manifest";
        case UPDATE_STAGE_CLOSE_MANIFEST:
            return "close-manifest";
        case UPDATE_STAGE_VERIFY_MANIFEST:
            return "verify-manifest";
        case UPDATE_STAGE_SELECT_TARGET:
            return "select-target";
        case UPDATE_STAGE_OPEN_APP:
            return "open-app";
        case UPDATE_STAGE_PREPARE_APP:
            return "prepare-app";
        case UPDATE_STAGE_HASH_APP:
            return "hash-app";
        case UPDATE_STAGE_OPEN_APP_RELOCATIONS:
            return "open-app-relocations";
        case UPDATE_STAGE_PREPARE_APP_RELOCATIONS:
            return "prepare-app-relocations";
        case UPDATE_STAGE_READ_APP_RELOCATIONS:
            return "read-app-relocations";
        case UPDATE_STAGE_VALIDATE_APP_RELOCATION:
            return "validate-app-relocation";
        case UPDATE_STAGE_READ_APP_RELOCATION_WORD:
            return "read-app-relocation-word";
        case UPDATE_STAGE_CHECK_APP_RELOCATION_WORD:
            return "check-app-relocation-word";
        case UPDATE_STAGE_CLOSE_APP:
            return "close-app";
        case UPDATE_STAGE_OPEN_APP_RELOCATION_WORDS:
            return "open-app-relocation-words";
        case UPDATE_STAGE_CLOSE_APP_RELOCATION_WORDS:
            return "close-app-relocation-words";
        case UPDATE_STAGE_CLOSE_APP_RELOCATIONS:
            return "close-app-relocations";
        case UPDATE_STAGE_OPEN_GUI:
            return "open-gui";
        case UPDATE_STAGE_PREPARE_GUI:
            return "prepare-gui";
        case UPDATE_STAGE_HASH_GUI:
            return "hash-gui";
        case UPDATE_STAGE_CLOSE_GUI_SOURCE:
            return "close-gui-source";
        case UPDATE_STAGE_ERASE_APP_START:
            return "erase-app-start";
        case UPDATE_STAGE_ERASE_APP_POLL:
            return "erase-app-poll";
        case UPDATE_STAGE_PROGRAM_APP_START:
            return "program-app-start";
        case UPDATE_STAGE_PROGRAM_APP_POLL:
            return "program-app-poll";
        case UPDATE_STAGE_READ_APP_TARGET:
            return "read-app-target";
        case UPDATE_STAGE_FINISH_APP_TARGET:
            return "finish-app-target";
        case UPDATE_STAGE_OPEN_APP_PROGRAM:
            return "open-app-program";
        case UPDATE_STAGE_READ_APP_PROGRAM_BLOCK:
            return "read-app-program-block";
        case UPDATE_STAGE_APPLY_APP_PROGRAM_BLOCK:
            return "apply-app-program-block";
        case UPDATE_STAGE_CLOSE_APP_PROGRAM:
            return "close-app-program";
        case UPDATE_STAGE_ERASE_GUI_START:
            return "erase-gui-start";
        case UPDATE_STAGE_ERASE_GUI_POLL:
            return "erase-gui-poll";
        case UPDATE_STAGE_PROGRAM_GUI_START:
            return "program-gui-start";
        case UPDATE_STAGE_PROGRAM_GUI_POLL:
            return "program-gui-poll";
        case UPDATE_STAGE_READ_GUI_TARGET:
            return "read-gui-target";
        case UPDATE_STAGE_FINISH_GUI_TARGET:
            return "finish-gui-target";
        case UPDATE_STAGE_OPEN_GUI_PROGRAM:
            return "open-gui-program";
        case UPDATE_STAGE_READ_GUI_PROGRAM_BLOCK:
            return "read-gui-program-block";
        case UPDATE_STAGE_CLOSE_GUI_PROGRAM:
            return "close-gui-program";
        case UPDATE_STAGE_CLEANUP_CLOSE:
            return "cleanup-close";
        default:
            return "unknown";
    }
}

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static void FinishFailure(update_service_t *service)
{
    service->state               = SERVICE_RUN_STATE_FAILED;
    service->result.status       = service->failure_status;
    service->result.error        = service->failure_error;
    service->result.stage        = service->failure_stage;
    service->result.native_error = (int32_t) service->failure_status;
    service->stage               = UPDATE_STAGE_IDLE;
}

static void FinishCancelled(update_service_t *service)
{
    LOG_WARN("update", "cancelled: stage=%s", UpdateStageName(service->stage));
    service->state               = SERVICE_RUN_STATE_CANCELLED;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = 0;
    service->stage               = UPDATE_STAGE_IDLE;
}

static void BeginFailure(update_service_t *service, firmware_status_t status, boot_error_t error)
{
    if (service->failure_pending != 0)
    {
        return;
    }
    LOG_ERROR("update", "failure: status=%d error=%d stage=%s cleanup=%d",
              (int)status, (int)error, UpdateStageName(service->stage),
              service->file_open);
    service->failure_status  = status;
    service->failure_error   = error;
    service->failure_stage   = (uint32_t) service->stage;
    service->failure_pending = 1;
    if (service->file_open != 0)
    {
        service->stage = UPDATE_STAGE_CLEANUP_CLOSE;
    }
    else
    {
        FinishFailure(service);
    }
}

static void BeginCancel(update_service_t *service)
{
    LOG_WARN("update", "cancel requested: stage=%s", UpdateStageName(service->stage));
    service->cancel_requested = 1;
    if (service->file_open != 0)
    {
        service->stage = UPDATE_STAGE_CLEANUP_CLOSE;
    }
    else
    {
        FinishCancelled(service);
    }
}

static firmware_status_t HashReset(update_service_t *service)
{
    return service->hash->reset(service->hash->context);
}

static firmware_status_t HashUpdate(update_service_t *service, const void *data, uint32_t size)
{
    return service->hash->update(service->hash->context, data, (size_t) size);
}

static firmware_status_t HashFinish(update_service_t *service,
                                    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    return service->hash->finish(service->hash->context, digest);
}

static firmware_status_t ChecksumReset(update_service_t *service)
{
    return service->checksum->reset(service->checksum->context);
}

static firmware_status_t ChecksumUpdate(update_service_t *service, const void *data, uint32_t size)
{
    return service->checksum->update(service->checksum->context, data, (size_t) size);
}

static firmware_status_t ChecksumFinish(update_service_t *service, uint32_t *value)
{
    return service->checksum->get_value(service->checksum->context, value);
}

static uint32_t MinimumU32(uint32_t lhs, uint32_t rhs)
{
    return (lhs < rhs) ? lhs : rhs;
}

static uint32_t IoChunkCapacity(const update_service_t *service)
{
    return MinimumU32(service->io_buffer_size, UPDATE_SERVICE_IO_BUFFER_MIN_SIZE);
}

static firmware_status_t ReadExactPackage(update_service_t *service, uint32_t offset, void *data,
                                          uint32_t size, boot_error_t error)
{
    uint32_t bytes_read      = 0U;
    firmware_status_t status = service->package_source->read_at(service->package_source->context,
                                                                offset, data, size, &bytes_read);

    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, error);
        return status;
    }
    if (bytes_read != size)
    {
        BeginFailure(service, FIRMWARE_STATUS_IO_ERROR, error);
        return FIRMWARE_STATUS_IO_ERROR;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadExactStorage(update_service_t *service, uint32_t address, void *data,
                                          uint32_t size, boot_error_t error)
{
    firmware_status_t status =
        service->storage->read(service->storage->context, address, data, size);

    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, error);
    }
    return status;
}

static void CloseOrFail(update_service_t *service, update_stage_t next)
{
    firmware_status_t status = service->package_source->close(service->package_source->context);

    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, BOOT_ERROR_MEDIA_UNAVAILABLE);
        return;
    }
    service->file_open = 0;
    service->stage     = next;
}

static void PollEraseOrProgram(update_service_t *service, update_stage_t next_start,
                               update_stage_t next_poll, boot_error_t error, uint32_t *progress,
                               uint32_t progress_size)
{
    async_block_device_operation_result_t operation;
    firmware_status_t status = service->storage->poll(service->storage->context);

    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, error);
        return;
    }
    status = service->storage->get_operation_result(service->storage->context, &operation);
    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, error);
        return;
    }
    if (operation.state == ASYNC_BLOCK_DEVICE_OPERATION_BUSY)
    {
        service->stage = next_poll;
        return;
    }
    if ((operation.state != ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED) ||
        !FirmwareStatus_IsOk(operation.status))
    {
        BeginFailure(service,
                     FirmwareStatus_IsOk(operation.status) ? FIRMWARE_STATUS_IO_ERROR
                                                           : operation.status,
                     error);
        return;
    }
    *progress += progress_size;
    service->stage = next_start;
}

static void StartErase(update_service_t *service, const boot_region_t *region,
                       update_stage_t next_after_complete, update_stage_t next_poll,
                       boot_error_t error)
{
    firmware_status_t status;

    if (service->erase_offset >= region->capacity_bytes)
    {
        LOG_INFO("update", "erase complete: next=%s",
                 UpdateStageName(next_after_complete));
        service->erase_offset = 0U;
        service->stage        = next_after_complete;
        return;
    }
    LOG_DEBUG("update", "erase start: address=0x%08lx size=%lu",
              (unsigned long)(region->flash_offset + service->erase_offset),
              (unsigned long)service->storage_info.erase_size);
    status = service->storage->erase_start(service->storage->context,
                                           region->flash_offset + service->erase_offset,
                                           service->storage_info.erase_size);
    if (!FirmwareStatus_IsOk(status))
    {
        BeginFailure(service, status, error);
        return;
    }
    service->erase_started = 1;
    service->stage         = next_poll;
}

static firmware_status_t StartProgram(update_service_t *service, uint32_t address,
                                      const uint8_t *data, uint32_t remaining, uint32_t *size)
{
    uint32_t page_remaining =
        service->storage_info.program_size - (address % service->storage_info.program_size);
    *size = MinimumU32(remaining, page_remaining);
    return service->storage->program_start(service->storage->context, address, data, *size);
}

static void UpdateServiceStep(update_service_t *service)
{
    firmware_status_t status;

    switch (service->stage)
    {
        case UPDATE_STAGE_OPEN_MANIFEST:
            LOG_INFO("update", "opening manifest: %s", service->manifest_path);
            status = service->package_source->open(service->package_source->context,
                                                   service->manifest_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_MANIFEST_FORMAT);
            }
            else
            {
                service->file_open           = 1;
                service->manifest_size_known = 0;
                service->manifest_offset     = 0U;
                service->stage               = UPDATE_STAGE_READ_MANIFEST;
            }
            break;

        case UPDATE_STAGE_READ_MANIFEST:
            if (service->manifest_size_known == 0)
            {
                status = service->package_source->get_size(service->package_source->context,
                                                           &service->manifest_size);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_MANIFEST_FORMAT);
                }
                else if ((service->manifest_size == 0U) ||
                         (service->manifest_size > UPDATE_SERVICE_MANIFEST_MAX_SIZE) ||
                         (service->manifest_size > service->manifest_buffer_size))
                {
                    BeginFailure(service, FIRMWARE_STATUS_OUT_OF_RANGE, BOOT_ERROR_MANIFEST_FORMAT);
                }
                else
                {
                    service->manifest_size_known = 1;
                    LOG_INFO("update", "manifest size: %lu bytes",
                             (unsigned long)service->manifest_size);
                }
                break;
            }
            if (service->manifest_offset < service->manifest_size)
            {
                uint32_t size       = MinimumU32(IoChunkCapacity(service),
                                                 service->manifest_size - service->manifest_offset);
                uint32_t bytes_read = 0U;

                status = service->package_source->read_at(
                    service->package_source->context, service->manifest_offset,
                    &service->manifest_buffer[service->manifest_offset], size, &bytes_read);
                if (!FirmwareStatus_IsOk(status) || (bytes_read != size))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                                 BOOT_ERROR_MANIFEST_FORMAT);
                }
                else
                {
                    service->manifest_offset += size;
                }
                break;
            }
            service->stage = UPDATE_STAGE_CLOSE_MANIFEST;
            break;

        case UPDATE_STAGE_CLOSE_MANIFEST:
            CloseOrFail(service, UPDATE_STAGE_VERIFY_MANIFEST);
            break;

        case UPDATE_STAGE_VERIFY_MANIFEST:
            status =
                ManifestService_ParseAndValidate(service->manifest_service, service->manifest_buffer,
                                                 service->manifest_size, &service->manifest);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_MANIFEST_FORMAT);
            }
            else
            {
                service->manifest_prepared      = 1;
                service->state                  = SERVICE_RUN_STATE_SUCCEEDED;
                service->result.status          = FIRMWARE_STATUS_OK;
                service->result.error           = BOOT_ERROR_NONE;
                service->result.stage           = (uint32_t)service->stage;
                service->result.native_error    = 0;
                service->stage                  = UPDATE_STAGE_IDLE;
                LOG_INFO("update", "manifest verified: version=%u.%u.%u build=%lu app=%lu gui=%lu",
                         (unsigned int)service->manifest.release_version.major,
                         (unsigned int)service->manifest.release_version.minor,
                         (unsigned int)service->manifest.release_version.patch,
                         (unsigned long)service->manifest.build_number,
                         (unsigned long)service->manifest.app.image_size_bytes,
                         (unsigned long)service->manifest.gui.file_size_bytes);
            }
            break;

        case UPDATE_STAGE_SELECT_TARGET:
            status = (service->initial_install != 0)
                         ? SlotPolicy_GetPairLayout(service->initial_target_pair,
                                                    &service->target_layout)
                         : SlotPolicy_SelectInactivePair(service->active_record.active_pair,
                                                         &service->target_layout.pair);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_INTERNAL);
            }
            else if (service->initial_install != 0)
            {
                LOG_INFO("update", "target selected: pair=%s app=0x%08lx gui=0x%08lx",
                         BootPairName(service->target_layout.pair),
                         (unsigned long)service->target_layout.app.flash_offset,
                         (unsigned long)service->target_layout.gui.flash_offset);
                service->stage = UPDATE_STAGE_OPEN_APP;
            }
            else
            {
                status =
                    SlotPolicy_GetPairLayout(service->target_layout.pair, &service->target_layout);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_INTERNAL);
                }
                else
                {
                    LOG_INFO("update", "target selected: pair=%s app=0x%08lx gui=0x%08lx",
                             BootPairName(service->target_layout.pair),
                             (unsigned long)service->target_layout.app.flash_offset,
                             (unsigned long)service->target_layout.gui.flash_offset);
                    service->stage = UPDATE_STAGE_OPEN_APP;
                }
            }
            break;

        case UPDATE_STAGE_OPEN_APP:
            LOG_INFO("update", "opening app package: %s", service->app_path);
            status =
                service->package_source->open(service->package_source->context, service->app_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_APP_SOURCE_HASH);
            }
            else
            {
                service->file_open = 1;
                service->stage     = UPDATE_STAGE_PREPARE_APP;
            }
            break;

        case UPDATE_STAGE_PREPARE_APP:
            status = service->package_source->get_size(service->package_source->context,
                                                       &service->file_size);
            if (!FirmwareStatus_IsOk(status) ||
                (service->file_size != service->manifest.app.file_size_bytes) ||
                (service->file_size != service->manifest.app.image_size_bytes))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_APP_SOURCE_HASH);
            }
            else
            {
                uint32_t reset_handler;

                status = ReadExactPackage(service, 0U, service->io_buffer, 8U,
                                          BOOT_ERROR_RELOCATION_RANGE);
                if (!FirmwareStatus_IsOk(status))
                {
                    break;
                }
                reset_handler = ReadU32(&service->io_buffer[4]);
                if (((reset_handler & 1U) == 0U) ||
                    ((reset_handler & ~1UL) < service->manifest.app.link_address) ||
                    (((reset_handler & ~1UL) - service->manifest.app.link_address) !=
                     service->manifest.app.entry_offset))
                {
                    BeginFailure(service, FIRMWARE_STATUS_INVALID_STATE,
                                 BOOT_ERROR_RELOCATION_RANGE);
                    break;
                }
                service->file_offset = 0U;
                status = HashReset(service);
                if (!FirmwareStatus_IsOk(status) ||
                    !FirmwareStatus_IsOk(ChecksumReset(service)))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                                 BOOT_ERROR_APP_SOURCE_HASH);
                }
                else
                {
                    LOG_INFO("update", "app package size: %lu bytes entry=0x%08lx",
                             (unsigned long)service->file_size,
                             (unsigned long)service->manifest.app.entry_offset);
                    service->stage = UPDATE_STAGE_HASH_APP;
                }
            }
            break;

        case UPDATE_STAGE_HASH_APP:
            if (service->file_offset >= service->file_size)
            {
                uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE];

                status = HashFinish(service, digest);
                if (!FirmwareStatus_IsOk(status) ||
                    (memcmp(digest, service->manifest.app.sha256, sizeof(digest)) != 0) ||
                    !FirmwareStatus_IsOk(ChecksumFinish(service, &service->app_source_crc)) ||
                    (service->app_source_crc != service->manifest.app.source_crc32))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                                             : status,
                                 BOOT_ERROR_APP_SOURCE_HASH);
                }
                else
                {
                    LOG_INFO("update", "app source verified: size=%lu crc=0x%08lx",
                             (unsigned long)service->file_size,
                             (unsigned long)service->app_source_crc);
                    service->stage = UPDATE_STAGE_CLOSE_APP;
                }
                break;
            }
            service->file_chunk_size =
                MinimumU32(IoChunkCapacity(service), service->file_size - service->file_offset);
            status = ReadExactPackage(service, service->file_offset, service->io_buffer,
                                      service->file_chunk_size, BOOT_ERROR_APP_SOURCE_HASH);
            if (FirmwareStatus_IsOk(status))
            {
                status = HashUpdate(service, service->io_buffer, service->file_chunk_size);
                if (FirmwareStatus_IsOk(status))
                {
                    status = ChecksumUpdate(service, service->io_buffer, service->file_chunk_size);
                }
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_APP_SOURCE_HASH);
                }
                else
                {
                    service->file_offset += service->file_chunk_size;
                }
            }
            break;

        case UPDATE_STAGE_OPEN_APP_RELOCATIONS:
            LOG_INFO("update", "opening relocation table: %s", service->relocation_path);
            status = service->package_source->open(service->package_source->context,
                                                    service->relocation_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_RELOCATION_FORMAT);
            }
            else
            {
                service->file_open = 1;
                service->stage = UPDATE_STAGE_PREPARE_APP_RELOCATIONS;
            }
            break;

        case UPDATE_STAGE_PREPARE_APP_RELOCATIONS:
            status = service->package_source->get_size(service->package_source->context,
                                                       &service->file_size);
            if (!FirmwareStatus_IsOk(status) ||
                (service->manifest.app.relocation_count > service->relocation_entry_capacity) ||
                (service->manifest.app.relocation_count > 0U &&
                 service->file_size != service->manifest.app.relocation_count * HMI_RELOCATION_ENTRY_SIZE) ||
                (service->manifest.app.relocation_count == 0U && service->file_size != 0U))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_RELOCATION_FORMAT);
            }
            else
            {
                service->relocation_bytes = service->file_size;
                service->file_offset = 0U;
                status = ChecksumReset(service);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_RELOCATION_FORMAT);
                }
                else
                {
                    service->stage = UPDATE_STAGE_READ_APP_RELOCATIONS;
                }
            }
            break;

        case UPDATE_STAGE_READ_APP_RELOCATIONS:
            if (service->file_offset >= service->relocation_bytes)
            {
                status = ChecksumFinish(service, &service->app_relocation_crc);
                if (!FirmwareStatus_IsOk(status) ||
                    (service->app_relocation_crc != service->manifest.app.relocation_crc32))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                                 BOOT_ERROR_RELOCATION_FORMAT);
                }
                else
                {
                    service->relocation_index = 0U;
                    service->stage = UPDATE_STAGE_CLOSE_APP_RELOCATIONS;
                }
                break;
            }
            status = ReadExactPackage(service, service->file_offset,
                                      service->relocation_buffer, service->relocation_bytes,
                                      BOOT_ERROR_RELOCATION_FORMAT);
            if (FirmwareStatus_IsOk(status))
            {
                status = ChecksumUpdate(service, service->relocation_buffer,
                                        service->relocation_bytes);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_RELOCATION_FORMAT);
                }
                else
                {
                    service->file_offset += service->relocation_bytes;
                }
            }
            break;

        case UPDATE_STAGE_CLOSE_APP_RELOCATIONS:
            CloseOrFail(service,
                        (service->manifest.app.relocation_count == 0U)
                            ? UPDATE_STAGE_OPEN_GUI
                            : UPDATE_STAGE_OPEN_APP_RELOCATION_WORDS);
            break;

        case UPDATE_STAGE_OPEN_APP_RELOCATION_WORDS:
            status = service->package_source->open(service->package_source->context,
                                                   service->app_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_RELOCATION_RANGE);
            }
            else
            {
                service->file_open = 1;
                service->relocation_index = 0U;
                service->stage = UPDATE_STAGE_VALIDATE_APP_RELOCATION;
            }
            break;

        case UPDATE_STAGE_VALIDATE_APP_RELOCATION:
            if (service->relocation_index >= service->manifest.app.relocation_count)
            {
                LOG_INFO("update", "app relocations verified: count=%lu",
                         (unsigned long)service->manifest.app.relocation_count);
                service->stage = UPDATE_STAGE_CLOSE_APP_RELOCATION_WORDS;
                break;
            }
            status = RelocationService_DecodeEntry(
                &service->relocation_buffer[service->relocation_index * HMI_RELOCATION_ENTRY_SIZE],
                &service->pending_relocation);
            if (!FirmwareStatus_IsOk(status) ||
                (service->pending_relocation.type != HMI_RELOCATION_ABS32_ADD_XIP_BASE) ||
                (service->pending_relocation.reserved != 0U) ||
                (service->pending_relocation.target_offset == 0U) ||
                ((service->pending_relocation.target_offset & 3U) != 0U) ||
                ((service->relocation_index == 0U) &&
                 (service->pending_relocation.target_offset != 4U)) ||
                ((service->relocation_index != 0U) &&
                 (service->pending_relocation.target_offset <=
                  service->relocation_entries[service->relocation_index - 1U].target_offset)))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_RELOCATION_FORMAT);
                break;
            }
            if (!CheckedArithmetic_AddU32(service->pending_relocation.target_offset,
                                          sizeof(uint32_t), &service->pending_relocation_word) ||
                (service->pending_relocation_word > service->manifest.app.image_size_bytes))
            {
                BeginFailure(service, FIRMWARE_STATUS_OUT_OF_RANGE, BOOT_ERROR_RELOCATION_RANGE);
                break;
            }
            service->stage = UPDATE_STAGE_READ_APP_RELOCATION_WORD;
            break;

        case UPDATE_STAGE_READ_APP_RELOCATION_WORD:
            status = ReadExactPackage(
                service, service->pending_relocation.target_offset,
                service->io_buffer, sizeof(uint32_t), BOOT_ERROR_RELOCATION_RANGE);
            if (FirmwareStatus_IsOk(status))
            {
                service->stage = UPDATE_STAGE_CHECK_APP_RELOCATION_WORD;
            }
            break;

        case UPDATE_STAGE_CHECK_APP_RELOCATION_WORD:
        {
            uint32_t raw_word = ReadU32(service->io_buffer);
            uint32_t canonical_word;
            uint32_t xip_end;

            if (!CheckedArithmetic_AddU32(service->manifest.app.link_address,
                                          SLOT_POLICY_FLASH_CAPACITY_BYTES,
                                          &xip_end) ||
                (raw_word < service->manifest.app.link_address) ||
                (raw_word >= xip_end) ||
                ((canonical_word = raw_word - service->manifest.app.link_address),
                 !CheckedArithmetic_AddU32(canonical_word,
                                           service->target_layout.app.mapped_address,
                                           &service->pending_relocation_word)) ||
                (service->pending_relocation_word >= xip_end))
            {
                BeginFailure(service, FIRMWARE_STATUS_OUT_OF_RANGE, BOOT_ERROR_RELOCATION_RANGE);
                break;
            }
            service->relocation_entries[service->relocation_index] = service->pending_relocation;
            ++service->relocation_index;
            service->stage = UPDATE_STAGE_VALIDATE_APP_RELOCATION;
            break;
        }

        case UPDATE_STAGE_CLOSE_APP_RELOCATION_WORDS:
            CloseOrFail(service, UPDATE_STAGE_OPEN_GUI);
            break;

        case UPDATE_STAGE_CLOSE_APP:
            CloseOrFail(service, UPDATE_STAGE_OPEN_GUI);
            break;

        case UPDATE_STAGE_OPEN_GUI:
            LOG_INFO("update", "opening gui package: %s", service->gui_path);
            status =
                service->package_source->open(service->package_source->context, service->gui_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_GUI_SOURCE_HASH);
            }
            else
            {
                service->file_open = 1;
                service->stage     = UPDATE_STAGE_PREPARE_GUI;
            }
            break;

        case UPDATE_STAGE_PREPARE_GUI:
            status = service->package_source->get_size(service->package_source->context,
                                                       &service->file_size);
            if (!FirmwareStatus_IsOk(status) ||
                (service->file_size != service->manifest.gui.file_size_bytes))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_GUI_SOURCE_HASH);
            }
            else
            {
                service->file_offset = 0U;
                status               = HashReset(service);
                if (!FirmwareStatus_IsOk(status) || !FirmwareStatus_IsOk(ChecksumReset(service)))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                                 BOOT_ERROR_GUI_SOURCE_HASH);
                }
                else
                {
                    LOG_INFO("update", "gui package size: %lu bytes",
                             (unsigned long)service->file_size);
                    service->stage = UPDATE_STAGE_HASH_GUI;
                }
            }
            break;

        case UPDATE_STAGE_HASH_GUI:
            if (service->file_offset >= service->file_size)
            {
                uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE];

                status = HashFinish(service, digest);
                if (!FirmwareStatus_IsOk(status) ||
                    (memcmp(digest, service->manifest.gui.sha256, sizeof(digest)) != 0) ||
                    !FirmwareStatus_IsOk(ChecksumFinish(service, &service->gui_source_crc)) ||
                    (service->gui_source_crc != service->manifest.gui.crc32))
                {
                    BeginFailure(service,
                                 FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                                             : status,
                                 BOOT_ERROR_GUI_SOURCE_HASH);
                }
                else
                {
                    LOG_INFO("update", "gui source verified: crc=0x%08lx",
                             (unsigned long)service->gui_source_crc);
                    service->stage = UPDATE_STAGE_CLOSE_GUI_SOURCE;
                }
                break;
            }
            service->file_chunk_size =
                MinimumU32(IoChunkCapacity(service), service->file_size - service->file_offset);
            status = ReadExactPackage(service, service->file_offset, service->io_buffer,
                                      service->file_chunk_size, BOOT_ERROR_GUI_SOURCE_HASH);
            if (!FirmwareStatus_IsOk(status))
            {
                break;
            }
            status = HashUpdate(service, service->io_buffer, service->file_chunk_size);
            if (FirmwareStatus_IsOk(status))
            {
                status = ChecksumUpdate(service, service->io_buffer, service->file_chunk_size);
            }
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_GUI_SOURCE_HASH);
            }
            else
            {
                service->file_offset += service->file_chunk_size;
            }
            break;

        case UPDATE_STAGE_CLOSE_GUI_SOURCE:
            CloseOrFail(service, UPDATE_STAGE_ERASE_APP_START);
            break;

        case UPDATE_STAGE_ERASE_APP_START:
            StartErase(service, &service->target_layout.app, UPDATE_STAGE_OPEN_APP_PROGRAM,
                       UPDATE_STAGE_ERASE_APP_POLL, BOOT_ERROR_APP_ERASE);
            break;

        case UPDATE_STAGE_ERASE_APP_POLL:
            PollEraseOrProgram(service, UPDATE_STAGE_ERASE_APP_START, UPDATE_STAGE_ERASE_APP_POLL,
                               BOOT_ERROR_APP_ERASE, &service->erase_offset,
                               service->storage_info.erase_size);
            break;

        case UPDATE_STAGE_OPEN_APP_PROGRAM:
            LOG_INFO("update", "programming app: pair=%s target=0x%08lx size=%lu",
                     BootPairName(service->target_layout.pair),
                     (unsigned long)service->target_layout.app.flash_offset,
                     (unsigned long)service->manifest.app.image_size_bytes);
            status =
                service->package_source->open(service->package_source->context, service->app_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_APP_SOURCE_HASH);
                break;
            }
            service->file_open              = 1;
            service->app_block_offset       = 0U;
            service->relocation_apply_index = 0U;
            status = RelocationService_InitEx(
                &service->relocation, service->manifest.app.image_size_bytes,
                service->manifest.app.link_address,
                service->target_layout.app.mapped_address);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_RELOCATION_FORMAT);
            }
            else
            {
                service->stage = UPDATE_STAGE_READ_APP_PROGRAM_BLOCK;
            }
            break;

        case UPDATE_STAGE_READ_APP_PROGRAM_BLOCK:
            if (service->app_block_offset >= service->manifest.app.image_size_bytes)
            {
                status = RelocationService_Finish(&service->relocation,
                                                  service->manifest.app.relocation_count);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_RELOCATION_FORMAT);
                }
                else
                {
                    service->stage = UPDATE_STAGE_CLOSE_APP_PROGRAM;
                }
                break;
            }
            service->app_block_size =
                MinimumU32(IoChunkCapacity(service),
                           service->manifest.app.image_size_bytes - service->app_block_offset);
            status = ReadExactPackage(service, service->app_block_offset,
                                      service->io_buffer, service->app_block_size,
                                      BOOT_ERROR_APP_SOURCE_HASH);
            if (FirmwareStatus_IsOk(status))
            {
                service->stage = UPDATE_STAGE_APPLY_APP_PROGRAM_BLOCK;
            }
            break;

        case UPDATE_STAGE_APPLY_APP_PROGRAM_BLOCK:
        {
            uint32_t index     = service->relocation_apply_index;
            uint32_t block_end = service->app_block_offset + service->app_block_size;
            uint32_t count;

            while ((index < service->manifest.app.relocation_count) &&
                   (service->relocation_entries[index].target_offset < block_end))
            {
                ++index;
            }
            count  = index - service->relocation_apply_index;
            status = RelocationService_ApplyBlock(
                &service->relocation, service->app_block_offset, service->io_buffer,
                service->app_block_size,
                (count == 0U) ? NULL
                              : &service->relocation_entries[service->relocation_apply_index],
                count);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status,
                             (status == FIRMWARE_STATUS_OUT_OF_RANGE)
                                 ? BOOT_ERROR_RELOCATION_RANGE
                                 : BOOT_ERROR_RELOCATION_FORMAT);
            }
            else
            {
                service->relocation_apply_index = index;
                service->program_offset         = 0U;
                service->stage                  = UPDATE_STAGE_PROGRAM_APP_START;
            }
            break;
        }

        case UPDATE_STAGE_PROGRAM_APP_START:
            if (service->program_offset >= service->app_block_size)
            {
                service->app_block_offset += service->app_block_size;
                service->stage = UPDATE_STAGE_READ_APP_PROGRAM_BLOCK;
                break;
            }
            status = StartProgram(service,
                                  service->target_layout.app.flash_offset +
                                      service->app_block_offset + service->program_offset,
                                  &service->io_buffer[service->program_offset],
                                  service->app_block_size - service->program_offset,
                                  &service->pending_program_size);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_APP_PROGRAM);
            }
            else
            {
                service->stage = UPDATE_STAGE_PROGRAM_APP_POLL;
            }
            break;

        case UPDATE_STAGE_PROGRAM_APP_POLL:
            PollEraseOrProgram(service, UPDATE_STAGE_PROGRAM_APP_START,
                               UPDATE_STAGE_PROGRAM_APP_POLL, BOOT_ERROR_APP_PROGRAM,
                               &service->program_offset, service->pending_program_size);
            break;

        case UPDATE_STAGE_CLOSE_APP_PROGRAM:
            CloseOrFail(service, UPDATE_STAGE_READ_APP_TARGET);
            if ((service->stage == UPDATE_STAGE_READ_APP_TARGET) &&
                !FirmwareStatus_IsOk(ChecksumReset(service)))
            {
                BeginFailure(service, FIRMWARE_STATUS_IO_ERROR, BOOT_ERROR_APP_TARGET_CRC);
            }
            else if (service->stage == UPDATE_STAGE_READ_APP_TARGET)
            {
                service->target_read_offset = 0U;
            }
            break;

        case UPDATE_STAGE_READ_APP_TARGET:
            if (service->target_read_offset >= service->manifest.app.image_size_bytes)
            {
                service->stage = UPDATE_STAGE_FINISH_APP_TARGET;
                break;
            }
            service->file_chunk_size =
                MinimumU32(IoChunkCapacity(service),
                           service->manifest.app.image_size_bytes - service->target_read_offset);
            status = ReadExactStorage(
                service, service->target_layout.app.flash_offset + service->target_read_offset,
                service->io_buffer, service->file_chunk_size, BOOT_ERROR_APP_TARGET_CRC);
            if (FirmwareStatus_IsOk(status))
            {
                status = ChecksumUpdate(service, service->io_buffer, service->file_chunk_size);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_APP_TARGET_CRC);
                }
                else
                {
                    service->target_read_offset += service->file_chunk_size;
                }
            }
            break;

        case UPDATE_STAGE_FINISH_APP_TARGET:
            status = ChecksumFinish(service, &service->target_app_crc);
            if (!FirmwareStatus_IsOk(status) ||
                (service->target_app_crc != ((service->target_layout.pair == BOOT_PAIR_1)
                                                 ? service->manifest.app.target_crc32_app1
                                                 : service->manifest.app.target_crc32_app2)))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_APP_TARGET_CRC);
            }
            else
            {
                LOG_INFO("update", "app target verified: crc=0x%08lx",
                         (unsigned long)service->target_app_crc);
                service->erase_offset = 0U;
                service->stage        = UPDATE_STAGE_ERASE_GUI_START;
            }
            break;

        case UPDATE_STAGE_ERASE_GUI_START:
            StartErase(service, &service->target_layout.gui, UPDATE_STAGE_OPEN_GUI_PROGRAM,
                       UPDATE_STAGE_ERASE_GUI_POLL, BOOT_ERROR_GUI_ERASE);
            break;

        case UPDATE_STAGE_ERASE_GUI_POLL:
            PollEraseOrProgram(service, UPDATE_STAGE_ERASE_GUI_START, UPDATE_STAGE_ERASE_GUI_POLL,
                               BOOT_ERROR_GUI_ERASE, &service->erase_offset,
                               service->storage_info.erase_size);
            break;

        case UPDATE_STAGE_OPEN_GUI_PROGRAM:
            LOG_INFO("update", "programming gui: pair=%s target=0x%08lx size=%lu",
                     BootPairName(service->target_layout.pair),
                     (unsigned long)service->target_layout.gui.flash_offset,
                     (unsigned long)service->manifest.gui.file_size_bytes);
            status =
                service->package_source->open(service->package_source->context, service->gui_path);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_GUI_SOURCE_HASH);
            }
            else
            {
                service->file_open          = 1;
                service->gui_program_offset = 0U;
                service->stage              = UPDATE_STAGE_READ_GUI_PROGRAM_BLOCK;
            }
            break;

        case UPDATE_STAGE_READ_GUI_PROGRAM_BLOCK:
            if (service->gui_program_offset >= service->manifest.gui.file_size_bytes)
            {
                service->stage = UPDATE_STAGE_CLOSE_GUI_PROGRAM;
                break;
            }
            service->gui_block_size =
                MinimumU32(IoChunkCapacity(service),
                           service->manifest.gui.file_size_bytes - service->gui_program_offset);
            status = ReadExactPackage(service, service->gui_program_offset, service->io_buffer,
                                      service->gui_block_size, BOOT_ERROR_GUI_SOURCE_HASH);
            if (FirmwareStatus_IsOk(status))
            {
                service->program_offset = 0U;
                service->stage          = UPDATE_STAGE_PROGRAM_GUI_START;
            }
            break;

        case UPDATE_STAGE_PROGRAM_GUI_START:
            if (service->program_offset >= service->gui_block_size)
            {
                service->gui_program_offset += service->gui_block_size;
                service->stage = UPDATE_STAGE_READ_GUI_PROGRAM_BLOCK;
                break;
            }
            status = StartProgram(service,
                                  service->target_layout.gui.flash_offset +
                                      service->gui_program_offset + service->program_offset,
                                  &service->io_buffer[service->program_offset],
                                  service->gui_block_size - service->program_offset,
                                  &service->pending_program_size);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_GUI_PROGRAM);
            }
            else
            {
                service->stage = UPDATE_STAGE_PROGRAM_GUI_POLL;
            }
            break;

        case UPDATE_STAGE_PROGRAM_GUI_POLL:
            PollEraseOrProgram(service, UPDATE_STAGE_PROGRAM_GUI_START,
                               UPDATE_STAGE_PROGRAM_GUI_POLL, BOOT_ERROR_GUI_PROGRAM,
                               &service->program_offset, service->pending_program_size);
            break;

        case UPDATE_STAGE_CLOSE_GUI_PROGRAM:
            CloseOrFail(service, UPDATE_STAGE_READ_GUI_TARGET);
            if ((service->stage == UPDATE_STAGE_READ_GUI_TARGET) &&
                !FirmwareStatus_IsOk(ChecksumReset(service)))
            {
                BeginFailure(service, FIRMWARE_STATUS_IO_ERROR, BOOT_ERROR_GUI_TARGET_CRC);
            }
            else if (service->stage == UPDATE_STAGE_READ_GUI_TARGET)
            {
                service->gui_target_read_offset = 0U;
            }
            break;

        case UPDATE_STAGE_READ_GUI_TARGET:
            if (service->gui_target_read_offset >= service->manifest.gui.file_size_bytes)
            {
                service->stage = UPDATE_STAGE_FINISH_GUI_TARGET;
                break;
            }
            service->file_chunk_size =
                MinimumU32(IoChunkCapacity(service),
                           service->manifest.gui.file_size_bytes - service->gui_target_read_offset);
            status = ReadExactStorage(
                service, service->target_layout.gui.flash_offset + service->gui_target_read_offset,
                service->io_buffer, service->file_chunk_size, BOOT_ERROR_GUI_TARGET_CRC);
            if (FirmwareStatus_IsOk(status))
            {
                status = ChecksumUpdate(service, service->io_buffer, service->file_chunk_size);
                if (!FirmwareStatus_IsOk(status))
                {
                    BeginFailure(service, status, BOOT_ERROR_GUI_TARGET_CRC);
                }
                else
                {
                    service->gui_target_read_offset += service->file_chunk_size;
                }
            }
            break;

        case UPDATE_STAGE_FINISH_GUI_TARGET:
            status = ChecksumFinish(service, &service->target_gui_crc);
            if (!FirmwareStatus_IsOk(status) ||
                (service->target_gui_crc != service->manifest.gui.crc32))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_GUI_TARGET_CRC);
            }
            else
            {
                memset(&service->candidate_record, 0,
                       sizeof(service->candidate_record));
                service->candidate_record.active_pair     = service->target_layout.pair;
                service->candidate_record.release_version = service->manifest.release_version;
                service->candidate_record.build_number    = service->manifest.build_number;
                service->candidate_record.app_size        = service->manifest.app.image_size_bytes;
                service->candidate_record.app_crc32       = service->target_app_crc;
                service->candidate_record.gui_size        = service->manifest.gui.file_size_bytes;
                service->candidate_record.gui_crc32       = service->target_gui_crc;
                memcpy(service->candidate_record.package_id_hash,
                       service->manifest.package_id_hash128,
                       sizeof(service->candidate_record.package_id_hash));
                memcpy(service->candidate_record.manifest_sha256,
                       service->manifest.manifest_sha256,
                       sizeof(service->candidate_record.manifest_sha256));
                service->state               = SERVICE_RUN_STATE_SUCCEEDED;
                service->result.status       = FIRMWARE_STATUS_OK;
                service->result.error        = BOOT_ERROR_NONE;
                service->result.stage        = (uint32_t)service->stage;
                service->result.native_error = 0;
                service->install_completed   = 1;
                service->stage               = UPDATE_STAGE_IDLE;
                LOG_INFO("update", "install completed: pair=%s app_crc=0x%08lx gui_crc=0x%08lx",
                         BootPairName(service->candidate_record.active_pair),
                         (unsigned long)service->candidate_record.app_crc32,
                         (unsigned long)service->candidate_record.gui_crc32);
            }
            break;

        case UPDATE_STAGE_CLEANUP_CLOSE:
            (void) service->package_source->close(service->package_source->context);
            service->file_open = 0;
            if (service->cancel_requested != 0)
            {
                FinishCancelled(service);
            }
            else
            {
                FinishFailure(service);
            }
            break;

        default:
            BeginFailure(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INTERNAL);
            break;
    }
}

firmware_status_t UpdateService_Init(update_service_t *service,
                                     const update_service_dependencies_t *dependencies)
{
    async_block_device_info_t info;
    firmware_status_t status;

    if ((service == NULL) || (dependencies == NULL) || (dependencies->package_source == NULL) ||
        (dependencies->package_source->open == NULL) ||
        (dependencies->package_source->close == NULL) ||
        (dependencies->package_source->get_size == NULL) ||
        (dependencies->package_source->read_at == NULL) || (dependencies->storage == NULL) ||
        (dependencies->storage->get_info == NULL) || (dependencies->storage->read == NULL) ||
        (dependencies->storage->program_start == NULL) ||
        (dependencies->storage->erase_start == NULL) || (dependencies->storage->poll == NULL) ||
        (dependencies->storage->get_operation_result == NULL) || (dependencies->checksum == NULL) ||
        (dependencies->checksum->reset == NULL) || (dependencies->checksum->update == NULL) ||
        (dependencies->checksum->get_value == NULL) || (dependencies->hash == NULL) ||
        (dependencies->hash->reset == NULL) || (dependencies->hash->update == NULL) ||
        (dependencies->hash->finish == NULL) || (dependencies->manifest_service == NULL) ||
        (dependencies->manifest_path == NULL) ||
        (dependencies->app_path == NULL) || (dependencies->relocation_path == NULL) ||
        (dependencies->gui_path == NULL) ||
        (dependencies->manifest_buffer == NULL) ||
        (dependencies->manifest_buffer_size < UPDATE_SERVICE_MANIFEST_MAX_SIZE) ||
        (dependencies->io_buffer == NULL) ||
        (dependencies->io_buffer_size < UPDATE_SERVICE_IO_BUFFER_MIN_SIZE) ||
        (dependencies->relocation_buffer == NULL) ||
        (dependencies->relocation_buffer_size <
         UPDATE_SERVICE_MAX_RELOCATIONS * HMI_RELOCATION_ENTRY_SIZE) ||
        (dependencies->relocation_entries == NULL) ||
        (dependencies->relocation_entry_capacity < UPDATE_SERVICE_MAX_RELOCATIONS))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = dependencies->storage->get_info(dependencies->storage->context, &info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((info.capacity_bytes != SLOT_POLICY_FLASH_CAPACITY_BYTES) || (info.program_size == 0U) ||
        (info.erase_size != SLOT_POLICY_ERASE_SIZE_BYTES))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    service->package_source            = dependencies->package_source;
    service->storage                   = dependencies->storage;
    service->storage_info              = info;
    service->checksum                  = dependencies->checksum;
    service->hash                      = dependencies->hash;
    service->manifest_service          = dependencies->manifest_service;
    service->manifest_path             = dependencies->manifest_path;
    service->app_path                  = dependencies->app_path;
    service->relocation_path           = dependencies->relocation_path;
    service->gui_path                  = dependencies->gui_path;
    service->manifest_buffer           = dependencies->manifest_buffer;
    service->manifest_buffer_size      = dependencies->manifest_buffer_size;
    service->io_buffer                 = dependencies->io_buffer;
    service->io_buffer_size            = dependencies->io_buffer_size;
    service->relocation_buffer         = dependencies->relocation_buffer;
    service->relocation_buffer_size    = dependencies->relocation_buffer_size;
    service->relocation_entries        = dependencies->relocation_entries;
    service->relocation_entry_capacity = dependencies->relocation_entry_capacity;
    service->state                     = SERVICE_RUN_STATE_IDLE;
    service->stage                     = UPDATE_STAGE_IDLE;
    service->initial_target_pair       = BOOT_PAIR_NONE;
    service->initial_install           = 0;
    service->result.status             = FIRMWARE_STATUS_OK;
    service->result.error              = BOOT_ERROR_NONE;
    service->result.stage              = UPDATE_STAGE_IDLE;
    service->result.native_error       = 0;
    service->initialized               = 1;
    LOG_INFO("update", "initialized: storage=%lu erase=%lu program=%lu",
             (unsigned long)info.capacity_bytes,
             (unsigned long)info.erase_size,
             (unsigned long)info.program_size);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *) service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) || (implementation->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    implementation->state                  = SERVICE_RUN_STATE_RUNNING;
    implementation->stage                  = UPDATE_STAGE_OPEN_MANIFEST;
    implementation->result.status          = FIRMWARE_STATUS_OK;
    implementation->result.error           = BOOT_ERROR_NONE;
    implementation->result.stage           = UPDATE_STAGE_OPEN_MANIFEST;
    implementation->result.native_error    = 0;
    implementation->failure_pending        = 0;
    implementation->cancel_requested       = 0;
    implementation->file_open              = 0;
    implementation->erase_started          = 0;
    implementation->manifest_size_known    = 0;
    implementation->manifest_prepared      = 0;
    implementation->install_completed      = 0;
    implementation->initial_install        = 0;
    implementation->initial_target_pair   = BOOT_PAIR_NONE;
    implementation->target_read_offset     = 0U;
    implementation->gui_target_read_offset = 0U;
    LOG_INFO("update", "prepare started");
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStart(
    struct update_service *service,
    const boot_active_record_t *active_record)
{
    boot_pair_layout_t layout;
    update_service_t *implementation = (update_service_t *)service;

    if ((implementation == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state == SERVICE_RUN_STATE_RUNNING) ||
        (implementation->manifest_prepared == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!FirmwareStatus_IsOk(SlotPolicy_GetPairLayout(active_record->active_pair, &layout)) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.app, active_record->app_size)) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.gui, active_record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    implementation->active_record = *active_record;
    implementation->initial_install = 0;
    implementation->initial_target_pair = BOOT_PAIR_NONE;
    implementation->state = SERVICE_RUN_STATE_RUNNING;
    implementation->stage = UPDATE_STAGE_SELECT_TARGET;
    implementation->result.status = FIRMWARE_STATUS_OK;
    implementation->result.error = BOOT_ERROR_NONE;
    implementation->result.stage = UPDATE_STAGE_SELECT_TARGET;
    implementation->result.native_error = 0;
    implementation->failure_pending = 0;
    implementation->cancel_requested = 0;
    implementation->erase_started = 0;
    implementation->install_completed = 0;
    LOG_INFO("update", "install started: active=%s",
             BootPairName(active_record->active_pair));
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InitialInstallStart(
    struct update_service *service,
    boot_pair_t target_pair)
{
    update_service_t *implementation = (update_service_t *)service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state == SERVICE_RUN_STATE_RUNNING) ||
        (implementation->manifest_prepared == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((target_pair != BOOT_PAIR_1) && (target_pair != BOOT_PAIR_2))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memset(&implementation->active_record, 0, sizeof(implementation->active_record));
    implementation->initial_install = 1;
    implementation->initial_target_pair = target_pair;
    implementation->state = SERVICE_RUN_STATE_RUNNING;
    implementation->stage = UPDATE_STAGE_SELECT_TARGET;
    implementation->result.status = FIRMWARE_STATUS_OK;
    implementation->result.error = BOOT_ERROR_NONE;
    implementation->result.stage = UPDATE_STAGE_SELECT_TARGET;
    implementation->result.native_error = 0;
    implementation->failure_pending = 0;
    implementation->cancel_requested = 0;
    implementation->erase_started = 0;
    implementation->install_completed = 0;
    LOG_INFO("update", "initial install started: target=%s",
             BootPairName(target_pair));
    return FIRMWARE_STATUS_OK;
}

void UpdateService_Process(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *) service;

    if ((implementation == NULL) || (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }
    UpdateServiceStep(implementation);
}

firmware_status_t UpdateService_Cancel(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *) service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) || (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (implementation->failure_pending != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (implementation->erase_started != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    BeginCancel(implementation);
    return FIRMWARE_STATUS_OK;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED
                             : ((const update_service_t *) service)->state;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    return (service == NULL) ? NULL : &((const update_service_t *) service)->result;
}

const validated_manifest_t *UpdateService_GetManifest(
    const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *)service;

    return ((implementation == NULL) || (implementation->manifest_prepared == 0))
               ? NULL
               : &implementation->manifest;
}

const boot_active_record_t *UpdateService_GetCandidate(
    const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *)service;

    return ((implementation == NULL) ||
            (implementation->state != SERVICE_RUN_STATE_SUCCEEDED) ||
            (implementation->install_completed == 0))
               ? NULL
               : &implementation->candidate_record;
}
