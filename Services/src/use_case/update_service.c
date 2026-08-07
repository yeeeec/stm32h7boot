/**
 * @file update_service.c
 * @brief Fixed APP/GUI runtime installer.
 */
#include "services/use_case/update_service.h"

#include <stddef.h>
#include <string.h>

#include "services/capability/update_request_service_api.h"

static uint32_t MinU32(uint32_t left, uint32_t right)
{
    return (left < right) ? left : right;
}

static void Fail(update_service_t *service, firmware_status_t status, boot_error_t error)
{
    if ((service->source_file_open != 0) && (service->package_source != NULL) &&
        (service->package_source->close != NULL))
    {
        (void)service->package_source->close(service->package_source->context);
        service->source_file_open = 0;
    }
    service->failure_status = status;
    service->failure_error = error;
    service->result.status = status;
    service->result.error = error;
    service->result.stage = (uint32_t)service->stage;
    service->result.native_error = 0;
    service->state = SERVICE_RUN_STATE_FAILED;
    service->candidate_ready = 0;
}

static firmware_status_t HashReset(update_service_t *service)
{
    return service->hash->reset(service->hash->context);
}

static firmware_status_t HashUpdate(update_service_t *service, const void *data, uint32_t size)
{
    return service->hash->update(service->hash->context, data, size);
}

static firmware_status_t HashFinish(update_service_t *service, uint8_t digest[32U])
{
    return service->hash->finish(service->hash->context, digest);
}

static firmware_status_t CloseSource(update_service_t *service)
{
    firmware_status_t status;

    if (service->source_file_open == 0)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = service->package_source->close(service->package_source->context);
    if (FirmwareStatus_IsOk(status))
    {
        service->source_file_open = 0;
    }
    return status;
}

static firmware_status_t ValidateRuntimeLayout(const async_block_device_info_t *storage_info,
                                                const boot_runtime_layout_t *layout)
{
    uint32_t end;

    if ((storage_info == NULL) || (layout == NULL) || (storage_info->capacity_bytes == 0U) ||
        (storage_info->program_size == 0U) || (storage_info->erase_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((layout->app_offset != BOOT_APP_FLASH_OFFSET) ||
        (layout->app_xip_base != BOOT_APP_RUNTIME_BASE) ||
        (layout->app_max_size != BOOT_APP_RUNTIME_SIZE) ||
        (layout->gui_offset != BOOT_GUI_FLASH_OFFSET) ||
        (layout->gui_mmap_base != BOOT_GUI_RUNTIME_BASE) ||
        (layout->gui_max_size != BOOT_GUI_RUNTIME_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((layout->app_offset % storage_info->erase_size) != 0U ||
        (layout->gui_offset % storage_info->erase_size) != 0U ||
        (layout->app_max_size % storage_info->erase_size) != 0U ||
        (layout->gui_max_size % storage_info->erase_size) != 0U ||
        (layout->app_offset > storage_info->capacity_bytes) ||
        (layout->app_max_size > (storage_info->capacity_bytes - layout->app_offset)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    end = layout->gui_offset;
    if ((end > storage_info->capacity_bytes) ||
        (layout->gui_max_size > (storage_info->capacity_bytes - end)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t GetSourceSize(update_service_t *service, uint32_t expected,
                                        uint32_t maximum, uint32_t *actual)
{
    firmware_status_t status = service->package_source->get_size(
        service->package_source->context, actual);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((*actual == 0U) || (*actual > maximum) || (*actual != expected))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StartErase(update_service_t *service, uint32_t base, uint32_t size,
                                    uint32_t offset)
{
    uint32_t erase_size = service->storage_info.erase_size;

    if (offset >= size)
    {
        return FIRMWARE_STATUS_OK;
    }
    return service->storage->erase_start(service->storage->context, base + offset, erase_size);
}

static firmware_status_t PollOperation(update_service_t *service,
                                       async_block_device_operation_result_t *operation)
{
    firmware_status_t status = service->storage->poll(service->storage->context);

    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return service->storage->get_operation_result(service->storage->context, operation);
}

static firmware_status_t NextProgramChunk(update_service_t *service, uint32_t runtime_size,
                                          uint32_t *chunk_size)
{
    uint32_t remaining;
    uint32_t page_remaining;

    if (service->program_offset >= service->active_source_size)
    {
        *chunk_size = 0U;
        return FIRMWARE_STATUS_OK;
    }
    remaining = service->active_source_size - service->program_offset;
    page_remaining = service->storage_info.program_size -
                     (service->program_offset % service->storage_info.program_size);
    *chunk_size = MinU32(remaining, MinU32(service->io_buffer_size, page_remaining));
    if ((*chunk_size == 0U) || (service->program_offset > runtime_size) ||
        (*chunk_size > (runtime_size - service->program_offset)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t StartProgramSource(update_service_t *service,
                                            package_file_id_t file,
                                            const manifest_app_component_t *component,
                                            uint32_t runtime_offset, uint32_t runtime_size)
{
    firmware_status_t status;

    status = service->package_source->open(service->package_source->context, file);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    service->source_file_open = 1;
    status = GetSourceSize(service, component->size_bytes, runtime_size,
                           &service->active_source_size);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    service->active_component = component;
    service->active_runtime_offset = runtime_offset;
    service->active_runtime_size = runtime_size;
    service->program_offset = 0U;
    return HashReset(service);
}

firmware_status_t UpdateService_Init(update_service_t *service,
                                     const update_service_dependencies_t *dependencies)
{
    firmware_status_t status;
    async_block_device_info_t info;

    if ((service == NULL) || (dependencies == NULL) || (dependencies->package_source == NULL) ||
        (dependencies->manifest_service == NULL) ||
        (dependencies->update_request_service == NULL) || (dependencies->hash == NULL) ||
        (dependencies->storage == NULL) || (dependencies->runtime_layout == NULL) ||
        (dependencies->manifest_buffer == NULL) || (dependencies->io_buffer == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((dependencies->package_source->open == NULL) ||
        (dependencies->package_source->close == NULL) ||
        (dependencies->package_source->get_size == NULL) ||
        (dependencies->package_source->read_at == NULL) ||
        (dependencies->hash->reset == NULL) || (dependencies->hash->update == NULL) ||
        (dependencies->hash->finish == NULL) || (dependencies->storage->get_info == NULL) ||
        (dependencies->storage->read == NULL) ||
        (dependencies->storage->program_start == NULL) ||
        (dependencies->storage->erase_start == NULL) || (dependencies->storage->poll == NULL) ||
        (dependencies->storage->get_operation_result == NULL))
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
    status = ValidateRuntimeLayout(&info, dependencies->runtime_layout);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((dependencies->manifest_buffer_size < MANIFEST_SERVICE_MAX_DOCUMENT_SIZE) ||
        (dependencies->io_buffer_size < info.program_size))
    {
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }

    memset(service, 0, sizeof(*service));
    service->package_source = dependencies->package_source;
    service->manifest_service = dependencies->manifest_service;
    service->update_request_service = dependencies->update_request_service;
    service->hash = dependencies->hash;
    service->storage = dependencies->storage;
    service->runtime_layout = dependencies->runtime_layout;
    service->storage_info = info;
    service->manifest_buffer = dependencies->manifest_buffer;
    service->manifest_buffer_size = dependencies->manifest_buffer_size;
    service->io_buffer = dependencies->io_buffer;
    service->io_buffer_size = dependencies->io_buffer_size;
    service->state = SERVICE_RUN_STATE_IDLE;
    service->stage = UPDATE_STAGE_IDLE;
    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service,
                                             const update_request_t *request)
{
    update_service_t *implementation = (update_service_t *)service;

    if ((implementation == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_IDLE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    implementation->request = *request;
    memset(&implementation->manifest, 0, sizeof(implementation->manifest));
    memset(&implementation->candidate_record, 0, sizeof(implementation->candidate_record));
    implementation->candidate_ready = 0;
    implementation->runtime_may_be_modified = 0;
    implementation->manifest_size = 0U;
    implementation->manifest_offset = 0U;
    implementation->stage = UPDATE_STAGE_PREPARE_MANIFEST_OPEN;
    implementation->state = SERVICE_RUN_STATE_RUNNING;
    implementation->result.status = FIRMWARE_STATUS_OK;
    implementation->result.error = BOOT_ERROR_NONE;
    implementation->result.stage = (uint32_t)implementation->stage;
    implementation->result.native_error = 0;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStart(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *)service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_SUCCEEDED) ||
        (implementation->stage != UPDATE_STAGE_PREPARED))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    implementation->candidate_ready = 0;
    implementation->runtime_may_be_modified = 0;
    implementation->stage = UPDATE_STAGE_SOURCE_APP_OPEN;
    implementation->state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

static void ProcessPrepare(update_service_t *service)
{
    firmware_status_t status;
    uint32_t chunk;
    uint32_t bytes_read;

    switch (service->stage)
    {
        case UPDATE_STAGE_PREPARE_MANIFEST_OPEN:
            status = service->package_source->open(service->package_source->context,
                                                   PACKAGE_FILE_MANIFEST);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_PREPARE);
                break;
            }
            service->source_file_open = 1;
            service->stage = UPDATE_STAGE_PREPARE_MANIFEST_SIZE;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_SIZE:
            status = service->package_source->get_size(service->package_source->context,
                                                       &service->manifest_size);
            if (!FirmwareStatus_IsOk(status) || (service->manifest_size == 0U) ||
                (service->manifest_size > service->manifest_buffer_size) ||
                (service->manifest_size > MANIFEST_SERVICE_MAX_DOCUMENT_SIZE))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_OUT_OF_RANGE : status,
                     BOOT_ERROR_PREPARE);
                break;
            }
            service->manifest_offset = 0U;
            service->stage = UPDATE_STAGE_PREPARE_MANIFEST_READ;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_READ:
            chunk = MinU32(service->io_buffer_size,
                           service->manifest_size - service->manifest_offset);
            bytes_read = 0U;
            status = service->package_source->read_at(
                service->package_source->context, service->manifest_offset,
                &service->manifest_buffer[service->manifest_offset], chunk, &bytes_read);
            if (!FirmwareStatus_IsOk(status) || (bytes_read != chunk))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                     BOOT_ERROR_PREPARE);
                break;
            }
            service->manifest_offset += chunk;
            if (service->manifest_offset >= service->manifest_size)
            {
                service->stage = UPDATE_STAGE_PREPARE_MANIFEST_CLOSE;
            }
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_CLOSE:
            status = CloseSource(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_PREPARE);
                break;
            }
            service->stage = UPDATE_STAGE_PREPARE_MANIFEST_PARSE;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_PARSE:
            status = ManifestService_ParseAndValidate(
                service->manifest_service, service->manifest_buffer, service->manifest_size,
                &service->manifest);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_PREPARE);
                break;
            }
            status = UpdateRequestService_ValidateManifestBinding(
                service->update_request_service, &service->request, service->manifest_buffer,
                service->manifest_size, &service->manifest);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_MANIFEST_BINDING);
                break;
            }
            service->stage = UPDATE_STAGE_PREPARED;
            service->state = SERVICE_RUN_STATE_SUCCEEDED;
            break;
        default:
            break;
    }
}

static void ProcessSourceHash(update_service_t *service, package_file_id_t file,
                              const manifest_app_component_t *component, uint32_t maximum_size,
                              update_stage_t size_stage,
                              update_stage_t hash_stage, update_stage_t verify_stage,
                              boot_error_t size_error, boot_error_t hash_error)
{
    firmware_status_t status;
    uint32_t bytes_read;
    uint32_t chunk;

    switch (service->stage)
    {
        case UPDATE_STAGE_SOURCE_APP_OPEN:
        case UPDATE_STAGE_SOURCE_GUI_OPEN:
            status = service->package_source->open(service->package_source->context, file);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->source_file_open = 1;
            service->stage = size_stage;
            break;
        case UPDATE_STAGE_SOURCE_APP_SIZE:
        case UPDATE_STAGE_SOURCE_GUI_SIZE:
            status = GetSourceSize(service, component->size_bytes, maximum_size,
                                   &service->active_source_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_OUT_OF_RANGE : status,
                     size_error);
                break;
            }
            service->source_offset = 0U;
            status = HashReset(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->stage = hash_stage;
            break;
        case UPDATE_STAGE_SOURCE_APP_HASH:
        case UPDATE_STAGE_SOURCE_GUI_HASH:
            chunk = MinU32(service->io_buffer_size,
                           service->active_source_size - service->source_offset);
            bytes_read = 0U;
            status = service->package_source->read_at(
                service->package_source->context, service->source_offset, service->io_buffer,
                chunk, &bytes_read);
            if (!FirmwareStatus_IsOk(status) || (bytes_read != chunk))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                     hash_error);
                break;
            }
            status = HashUpdate(service, service->io_buffer, chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->source_offset += chunk;
            if (service->source_offset >= service->active_source_size)
            {
                status = HashFinish(service, service->source_digest);
                if (!FirmwareStatus_IsOk(status))
                {
                    Fail(service, status, hash_error);
                }
                else
                {
                    service->stage = verify_stage;
                }
            }
            break;
        case UPDATE_STAGE_SOURCE_APP_VERIFY:
        case UPDATE_STAGE_SOURCE_GUI_VERIFY:
            if (memcmp(service->source_digest, component->sha256,
                       FIRMWARE_SHA256_DIGEST_SIZE) != 0)
            {
                Fail(service, FIRMWARE_STATUS_INVALID_STATE, hash_error);
                break;
            }
            status = CloseSource(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->stage = (file == PACKAGE_FILE_APP) ? UPDATE_STAGE_SOURCE_GUI_OPEN
                                                         : UPDATE_STAGE_APP_ERASE;
            if (file == PACKAGE_FILE_GUI)
            {
                service->erase_offset = 0U;
            }
            break;
        default:
            break;
    }
}

static void ProcessErase(update_service_t *service, int app)
{
    uint32_t base = app != 0 ? service->runtime_layout->app_offset
                             : service->runtime_layout->gui_offset;
    uint32_t size = app != 0 ? service->runtime_layout->app_max_size
                             : service->runtime_layout->gui_max_size;
    update_stage_t stage = app != 0 ? UPDATE_STAGE_APP_ERASE : UPDATE_STAGE_GUI_ERASE;
    update_stage_t poll_stage = app != 0 ? UPDATE_STAGE_APP_ERASE_POLL
                                         : UPDATE_STAGE_GUI_ERASE_POLL;
    update_stage_t next_stage = app != 0 ? UPDATE_STAGE_APP_PROGRAM_OPEN
                                         : UPDATE_STAGE_GUI_PROGRAM_OPEN;
    async_block_device_operation_result_t operation;
    firmware_status_t status;

    if (service->stage == stage)
    {
        if (service->erase_offset >= size)
        {
            service->stage = next_stage;
            return;
        }
        if (app != 0)
        {
            /* From this point onward any failure may leave Runtime partial. */
            service->runtime_may_be_modified = 1;
        }
        status = StartErase(service, base, size, service->erase_offset);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
            return;
        }
        service->stage = poll_stage;
        return;
    }

    status = PollOperation(service, &operation);
    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
    }
    else if (operation.state == ASYNC_BLOCK_DEVICE_OPERATION_BUSY)
    {
        return;
    }
    else if ((operation.state != ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED) ||
             !FirmwareStatus_IsOk(operation.status))
    {
        Fail(service, FirmwareStatus_IsOk(operation.status) ? FIRMWARE_STATUS_IO_ERROR
                                                             : operation.status,
             app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
    }
    else
    {
        service->erase_offset += service->storage_info.erase_size;
        service->stage = stage;
    }
}

static void ProcessProgram(update_service_t *service, int app)
{
    const package_file_id_t file = app != 0 ? PACKAGE_FILE_APP : PACKAGE_FILE_GUI;
    const manifest_app_component_t *component = app != 0 ? &service->manifest.app
                                                         : &service->manifest.gui;
    const boot_error_t error = app != 0 ? BOOT_ERROR_APP_PROGRAM : BOOT_ERROR_GUI_PROGRAM;
    const uint32_t base = app != 0 ? service->runtime_layout->app_offset
                                   : service->runtime_layout->gui_offset;
    const uint32_t runtime_size = app != 0 ? service->runtime_layout->app_max_size
                                           : service->runtime_layout->gui_max_size;
    const update_stage_t read_stage = app != 0 ? UPDATE_STAGE_APP_PROGRAM_READ
                                               : UPDATE_STAGE_GUI_PROGRAM_READ;
    const update_stage_t start_stage = app != 0 ? UPDATE_STAGE_APP_PROGRAM_START
                                                : UPDATE_STAGE_GUI_PROGRAM_START;
    const update_stage_t poll_stage = app != 0 ? UPDATE_STAGE_APP_PROGRAM_POLL
                                               : UPDATE_STAGE_GUI_PROGRAM_POLL;
    const update_stage_t hash_stage = app != 0 ? UPDATE_STAGE_APP_PROGRAM_HASH
                                               : UPDATE_STAGE_GUI_PROGRAM_HASH;
    const update_stage_t target_read_stage = app != 0 ? UPDATE_STAGE_APP_TARGET_READ
                                                       : UPDATE_STAGE_GUI_TARGET_READ;
    async_block_device_operation_result_t operation;
    firmware_status_t status;
    uint32_t chunk;
    uint32_t bytes_read;

    switch (service->stage)
    {
        case UPDATE_STAGE_APP_PROGRAM_OPEN:
        case UPDATE_STAGE_GUI_PROGRAM_OPEN:
            status = StartProgramSource(service, file, component, base, runtime_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
                break;
            }
            service->stage = read_stage;
            break;
        case UPDATE_STAGE_APP_PROGRAM_READ:
        case UPDATE_STAGE_GUI_PROGRAM_READ:
            status = NextProgramChunk(service, runtime_size, &chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
                break;
            }
            if (chunk == 0U)
            {
                service->stage = hash_stage;
                break;
            }
            bytes_read = 0U;
            status = service->package_source->read_at(
                service->package_source->context, service->program_offset, service->io_buffer,
                chunk, &bytes_read);
            if (!FirmwareStatus_IsOk(status) || (bytes_read != chunk))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status,
                     error);
                break;
            }
            status = HashUpdate(service, service->io_buffer, chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
                break;
            }
            service->pending_program_size = chunk;
            service->stage = start_stage;
            break;
        case UPDATE_STAGE_APP_PROGRAM_START:
        case UPDATE_STAGE_GUI_PROGRAM_START:
            status = service->storage->program_start(
                service->storage->context, base + service->program_offset, service->io_buffer,
                service->pending_program_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
                break;
            }
            service->stage = poll_stage;
            break;
        case UPDATE_STAGE_APP_PROGRAM_POLL:
        case UPDATE_STAGE_GUI_PROGRAM_POLL:
            status = PollOperation(service, &operation);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
            }
            else if (operation.state == ASYNC_BLOCK_DEVICE_OPERATION_BUSY)
            {
                return;
            }
            else if ((operation.state != ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED) ||
                     !FirmwareStatus_IsOk(operation.status))
            {
                Fail(service, FirmwareStatus_IsOk(operation.status) ? FIRMWARE_STATUS_IO_ERROR
                                                                     : operation.status,
                     error);
            }
            else
            {
                service->program_offset += service->pending_program_size;
                service->stage = read_stage;
            }
            break;
        case UPDATE_STAGE_APP_PROGRAM_HASH:
        case UPDATE_STAGE_GUI_PROGRAM_HASH:
            status = HashFinish(service, service->source_digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(service->source_digest, component->sha256,
                        FIRMWARE_SHA256_DIGEST_SIZE) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                     error);
                break;
            }
            status = CloseSource(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, error);
                break;
            }
            service->target_offset = 0U;
            status = HashReset(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, app != 0 ? BOOT_ERROR_APP_TARGET_HASH
                                               : BOOT_ERROR_GUI_TARGET_HASH);
                break;
            }
            service->stage = target_read_stage;
            break;
        default:
            break;
    }
}

static void ProcessTarget(update_service_t *service, int app)
{
    uint32_t size = app != 0 ? service->manifest.app.size_bytes : service->manifest.gui.size_bytes;
    uint32_t base = app != 0 ? service->runtime_layout->app_offset : service->runtime_layout->gui_offset;
    update_stage_t read_stage = app != 0 ? UPDATE_STAGE_APP_TARGET_READ : UPDATE_STAGE_GUI_TARGET_READ;
    update_stage_t hash_stage = app != 0 ? UPDATE_STAGE_APP_TARGET_HASH : UPDATE_STAGE_GUI_TARGET_HASH;
    boot_error_t read_error = app != 0 ? BOOT_ERROR_APP_TARGET_READ : BOOT_ERROR_GUI_TARGET_READ;
    boot_error_t hash_error = app != 0 ? BOOT_ERROR_APP_TARGET_HASH : BOOT_ERROR_GUI_TARGET_HASH;
    firmware_status_t status;
    uint32_t chunk;

    if (service->stage == read_stage)
    {
        if (service->target_offset >= size)
        {
            service->stage = hash_stage;
            return;
        }
        chunk = MinU32(service->io_buffer_size, size - service->target_offset);
        status = service->storage->read(service->storage->context, base + service->target_offset,
                                        service->io_buffer, chunk);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, read_error);
            return;
        }
        status = HashUpdate(service, service->io_buffer, chunk);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, hash_error);
            return;
        }
        service->target_offset += chunk;
        return;
    }

    status = HashFinish(service, service->target_digest);
    if (!FirmwareStatus_IsOk(status) ||
        (memcmp(service->target_digest,
                app != 0 ? service->manifest.app.sha256 : service->manifest.gui.sha256,
                FIRMWARE_SHA256_DIGEST_SIZE) != 0))
    {
        Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
             hash_error);
        return;
    }
    service->stage = app != 0 ? UPDATE_STAGE_GUI_ERASE : UPDATE_STAGE_BUILD_RECORD_CANDIDATE;
    service->erase_offset = 0U;
}

void UpdateService_Process(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *)service;
    int source_gui;

    if ((implementation == NULL) || (implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }
    if ((implementation->stage >= UPDATE_STAGE_PREPARE_MANIFEST_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_PREPARE_MANIFEST_PARSE))
    {
        ProcessPrepare(implementation);
        return;
    }
    if ((implementation->stage >= UPDATE_STAGE_SOURCE_APP_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_SOURCE_GUI_VERIFY))
    {
        source_gui = (implementation->stage >= UPDATE_STAGE_SOURCE_GUI_OPEN) ? 1 : 0;
        ProcessSourceHash(implementation,
                          source_gui != 0 ? PACKAGE_FILE_GUI : PACKAGE_FILE_APP,
                          source_gui != 0 ? &implementation->manifest.gui
                                          : &implementation->manifest.app,
                          source_gui != 0 ? implementation->runtime_layout->gui_max_size
                                          : implementation->runtime_layout->app_max_size,
                          source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_SIZE
                                          : UPDATE_STAGE_SOURCE_APP_SIZE,
                          source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_HASH
                                          : UPDATE_STAGE_SOURCE_APP_HASH,
                          source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_VERIFY
                                          : UPDATE_STAGE_SOURCE_APP_VERIFY,
                          source_gui != 0 ? BOOT_ERROR_GUI_SOURCE_SIZE
                                          : BOOT_ERROR_APP_SOURCE_SIZE,
                          source_gui != 0 ? BOOT_ERROR_GUI_SOURCE_HASH
                                          : BOOT_ERROR_APP_SOURCE_HASH);
        return;
    }
    if ((implementation->stage == UPDATE_STAGE_APP_ERASE) ||
        (implementation->stage == UPDATE_STAGE_APP_ERASE_POLL))
    {
        ProcessErase(implementation, 1);
        return;
    }
    if ((implementation->stage >= UPDATE_STAGE_APP_PROGRAM_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_APP_PROGRAM_HASH))
    {
        ProcessProgram(implementation, 1);
        return;
    }
    if ((implementation->stage == UPDATE_STAGE_APP_TARGET_READ) ||
        (implementation->stage == UPDATE_STAGE_APP_TARGET_HASH))
    {
        ProcessTarget(implementation, 1);
        return;
    }
    if ((implementation->stage == UPDATE_STAGE_GUI_ERASE) ||
        (implementation->stage == UPDATE_STAGE_GUI_ERASE_POLL))
    {
        ProcessErase(implementation, 0);
        return;
    }
    if ((implementation->stage >= UPDATE_STAGE_GUI_PROGRAM_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_GUI_PROGRAM_HASH))
    {
        ProcessProgram(implementation, 0);
        return;
    }
    if ((implementation->stage == UPDATE_STAGE_GUI_TARGET_READ) ||
        (implementation->stage == UPDATE_STAGE_GUI_TARGET_HASH))
    {
        ProcessTarget(implementation, 0);
        return;
    }
    if (implementation->stage == UPDATE_STAGE_BUILD_RECORD_CANDIDATE)
    {
        memset(&implementation->candidate_record, 0, sizeof(implementation->candidate_record));
        implementation->candidate_record.format_version = BOOT_ACTIVE_RECORD_FORMAT_V2;
        implementation->candidate_record.state = BOOT_ACTIVE_RECORD_STATE_VALID;
        implementation->candidate_record.release_version = implementation->manifest.release_version;
        implementation->candidate_record.build_number = implementation->manifest.build_number;
        implementation->candidate_record.app_size = implementation->manifest.app.size_bytes;
        implementation->candidate_record.gui_size = implementation->manifest.gui.size_bytes;
        memcpy(implementation->candidate_record.package_id_hash,
               implementation->manifest.package_id_hash128,
               sizeof(implementation->candidate_record.package_id_hash));
        memcpy(implementation->candidate_record.manifest_sha256,
               implementation->manifest.manifest_sha256,
               sizeof(implementation->candidate_record.manifest_sha256));
        memcpy(implementation->candidate_record.app_sha256, implementation->manifest.app.sha256,
               sizeof(implementation->candidate_record.app_sha256));
        memcpy(implementation->candidate_record.gui_sha256, implementation->manifest.gui.sha256,
               sizeof(implementation->candidate_record.gui_sha256));
        implementation->candidate_ready = 1;
        implementation->state = SERVICE_RUN_STATE_SUCCEEDED;
    }
}

firmware_status_t UpdateService_Cancel(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *)service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING) ||
        (implementation->stage >= UPDATE_STAGE_APP_ERASE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    (void)CloseSource(implementation);
    implementation->state = SERVICE_RUN_STATE_CANCELLED;
    implementation->stage = UPDATE_STAGE_IDLE;
    return FIRMWARE_STATUS_OK;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    return (service == NULL) ? NULL : &service->result;
}

const validated_manifest_t *UpdateService_GetManifest(const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *)service;

    return (implementation == NULL) || (implementation->stage < UPDATE_STAGE_PREPARED)
               ? NULL
               : &implementation->manifest;
}

const boot_active_record_t *UpdateService_GetCandidate(const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *)service;

    return (implementation == NULL) || (implementation->candidate_ready == 0)
               ? NULL
               : &implementation->candidate_record;
}

int UpdateService_RuntimeMayBeModified(const struct update_service *service)
{
    return (service != NULL) &&
           (((const update_service_t *)service)->runtime_may_be_modified != 0);
}
