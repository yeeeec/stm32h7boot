/**
 * @file secondary_mcu_update_service.c
 * @brief 外部 MCU 镜像的分块擦写和逐块回读校验状态机。
 *
 * 每次 Process 最多调用一个 Source 或 Programmer 回调。首次 erase 成功提交后，
 * target_may_be_modified 保持为真；任何后续失败都会先尝试 abort，保证 BOOT/RST
 * 和 UART 尽可能恢复到从 MCU 应用状态，同时保留最初失败作为权威结果。
 */
#include "services/use_case/secondary_mcu_update_service.h"

#include <stddef.h>
#include <string.h>

#include "services/common/update_request_types.h"

#define SECONDARY_MCU_REQUIRED_CAPABILITIES                                                        \
    (MCU_PROGRAMMER_CAPABILITY_READ | MCU_PROGRAMMER_CAPABILITY_WRITE |                            \
     MCU_PROGRAMMER_CAPABILITY_ERASE)

static uint32_t MinU32(uint32_t left, uint32_t right)
{
    return (left < right) ? left : right;
}

/** 把当前根因冻结下来，再转入唯一的会话恢复阶段。 */
static void BeginFailure(secondary_mcu_update_service_t *service, firmware_status_t status,
                         boot_error_t error)
{
    service->failure_status      = status;
    service->failure_error       = error;
    service->failure_stage       = service->stage;
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = 0;

    if (service->session_active != 0)
    {
        service->stage = SECONDARY_MCU_UPDATE_STAGE_ABORT;
    }
    else
    {
        service->state = SERVICE_RUN_STATE_FAILED;
        service->stage = SECONDARY_MCU_UPDATE_STAGE_IDLE;
    }
}

/** 进入失败或取消的稳定终态；abort 失败不覆盖原始业务根因。 */
static void ProcessAbort(secondary_mcu_update_service_t *service)
{
    firmware_status_t abort_status = service->programmer->abort(service->programmer->context);

    service->session_active = 0;
    if (service->cancel_requested != 0)
    {
        if (FirmwareStatus_IsOk(abort_status))
        {
            service->state         = SERVICE_RUN_STATE_CANCELLED;
            service->result.status = FIRMWARE_STATUS_OK;
            service->result.error  = BOOT_ERROR_NONE;
        }
        else
        {
            service->state         = SERVICE_RUN_STATE_FAILED;
            service->result.status = abort_status;
            service->result.error  = BOOT_ERROR_SECONDARY_MCU_EXIT;
        }
        service->result.stage = (uint32_t) SECONDARY_MCU_UPDATE_STAGE_ABORT;
    }
    else
    {
        service->state         = SERVICE_RUN_STATE_FAILED;
        service->result.status = service->failure_status;
        service->result.error  = service->failure_error;
        service->result.stage  = (uint32_t) service->failure_stage;
        if (!FirmwareStatus_IsOk(abort_status))
        {
            service->result.native_error = (int32_t) abort_status;
        }
    }
    service->stage = SECONDARY_MCU_UPDATE_STAGE_IDLE;
}

/** 校验 begin() 返回的限制，防止后续分块计算出现零进度。 */
static firmware_status_t ValidateProgrammerInfo(const mcu_programmer_info_t *info)
{
    if ((info->max_write_size == 0U) || (info->max_read_size == 0U) ||
        (info->max_erase_block_count == 0U) ||
        ((info->capabilities & SECONDARY_MCU_REQUIRED_CAPABILITIES) !=
         SECONDARY_MCU_REQUIRED_CAPABILITIES))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t
SecondaryMcuUpdateService_Init(secondary_mcu_update_service_t *service,
                               const secondary_mcu_update_service_dependencies_t *dependencies)
{
    if ((service == NULL) || (dependencies == NULL) || (dependencies->source == NULL) ||
        (dependencies->source->get_info == NULL) || (dependencies->source->read == NULL) ||
        (dependencies->programmer == NULL) || (dependencies->programmer->begin == NULL) ||
        (dependencies->programmer->erase == NULL) || (dependencies->programmer->write == NULL) ||
        (dependencies->programmer->read == NULL) || (dependencies->programmer->end == NULL) ||
        (dependencies->programmer->abort == NULL) || (dependencies->hash == NULL) ||
        (dependencies->hash->reset == NULL) || (dependencies->hash->update == NULL) ||
        (dependencies->hash->finish == NULL) || (dependencies->write_buffer == NULL) ||
        (dependencies->readback_buffer == NULL) ||
        (dependencies->write_buffer == dependencies->readback_buffer) ||
        (dependencies->buffer_size < SECONDARY_MCU_UPDATE_SERVICE_MIN_BUFFER_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    memset(service, 0, sizeof(*service));
    service->source          = dependencies->source;
    service->programmer      = dependencies->programmer;
    service->hash            = dependencies->hash;
    service->write_buffer    = dependencies->write_buffer;
    service->readback_buffer = dependencies->readback_buffer;
    service->buffer_size     = dependencies->buffer_size;
    service->state           = SERVICE_RUN_STATE_IDLE;
    service->result.status   = FIRMWARE_STATUS_OK;
    service->result.error    = BOOT_ERROR_NONE;
    service->stage           = SECONDARY_MCU_UPDATE_STAGE_IDLE;
    service->initialized     = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t SecondaryMcuUpdateService_Start(struct secondary_mcu_update_service *service,
                                                  const secondary_mcu_update_request_t *request)
{
    if ((service == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((request->image_size_bytes == 0U) ||
        (request->image_size_bytes > UPDATE_THERAPY_IMAGE_MAX_SIZE) ||
        (request->target_capacity_bytes == 0U) ||
        (request->image_size_bytes > request->target_capacity_bytes) ||
        (request->erase_page_count == 0U) ||
        (request->target_address > (UINT32_MAX - request->image_size_bytes)) ||
        (request->erase_page_start > (UINT32_MAX - request->erase_page_count)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    service->request = *request;
    memset(&service->source_info, 0, sizeof(service->source_info));
    memset(&service->programmer_info, 0, sizeof(service->programmer_info));
    service->source_offset          = 0U;
    service->erase_page_offset      = 0U;
    service->pending_size           = 0U;
    service->session_active         = 0;
    service->target_may_be_modified = 0;
    service->cancel_requested       = 0;
    service->failure_status         = FIRMWARE_STATUS_OK;
    service->failure_error          = BOOT_ERROR_NONE;
    service->failure_stage          = SECONDARY_MCU_UPDATE_STAGE_IDLE;
    service->result.status          = FIRMWARE_STATUS_OK;
    service->result.error           = BOOT_ERROR_NONE;
    service->result.stage           = 0U;
    service->result.native_error    = 0;
    service->state                  = SERVICE_RUN_STATE_RUNNING;
    service->stage                  = SECONDARY_MCU_UPDATE_STAGE_SOURCE_INFO;
    return FIRMWARE_STATUS_OK;
}

void SecondaryMcuUpdateService_Process(struct secondary_mcu_update_service *service)
{
    firmware_status_t status;
    uint32_t chunk;
    uint32_t remaining;

    if ((service == NULL) || (service->initialized == 0) ||
        (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case SECONDARY_MCU_UPDATE_STAGE_SOURCE_INFO:
            status = service->source->get_info(service->source->context,
                                                      &service->source_info);
            if (!FirmwareStatus_IsOk(status) || (service->source_info.size_bytes !=
                                                 service->request.image_size_bytes))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                                                         : status,
                             BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            service->source_offset = 0U;
            service->stage = SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_RESET;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_RESET:
            status = service->hash->reset(service->hash->context);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            service->source_offset = 0U;
            service->stage = SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_READ;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_READ:
            remaining = service->request.image_size_bytes - service->source_offset;
            if (remaining == 0U)
            {
                service->stage = SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_FINISH;
                break;
            }
            chunk = MinU32(remaining, service->buffer_size);
            status = service->source->read(service->source->context,
                                                  service->source_offset,
                                                  service->write_buffer, chunk);
            if (FirmwareStatus_IsOk(status))
            {
                status = service->hash->update(service->hash->context,
                                                      service->write_buffer, chunk);
            }
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            service->source_offset += chunk;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_FINISH:
            status = service->hash->finish(service->hash->context,
                                                  service->source_digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(service->source_digest, service->request.sha256,
                        FIRMWARE_SHA256_DIGEST_SIZE) != 0))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            /* Hashing consumed the source stream; programming always restarts at byte zero. */
            service->source_offset = 0U;
            service->stage = SECONDARY_MCU_UPDATE_STAGE_BEGIN;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_BEGIN:
            status = service->programmer->begin(service->programmer->context,
                                                       &service->programmer_info);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_ENTER);
                break;
            }
            service->session_active = 1;
            status = ValidateProgrammerInfo(&service->programmer_info);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_TARGET);
                break;
            }
            service->stage = SECONDARY_MCU_UPDATE_STAGE_ERASE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_ERASE:
            remaining =
                service->request.erase_page_count - service->erase_page_offset;
            if (remaining == 0U)
            {
                service->stage = SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE;
                break;
            }
            chunk  = MinU32(remaining, service->programmer_info.max_erase_block_count);
            status = service->programmer->erase(service->programmer->context,
                                                       service->request.erase_page_start +
                                                           service->erase_page_offset,
                                                       chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_ERASE);
                break;
            }
            service->target_may_be_modified = 1;
            service->erase_page_offset += chunk;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE:
            remaining = service->request.image_size_bytes - service->source_offset;
            if (remaining == 0U)
            {
                service->stage = SECONDARY_MCU_UPDATE_STAGE_END;
                break;
            }
            chunk  = MinU32(remaining, service->buffer_size);
            chunk  = MinU32(chunk, service->programmer_info.max_write_size);
            chunk  = MinU32(chunk, service->programmer_info.max_read_size);
            status = service->source->read(service->source->context,
                                                  service->source_offset,
                                                  service->write_buffer, chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            service->pending_size = chunk;
            service->stage        = SECONDARY_MCU_UPDATE_STAGE_WRITE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_WRITE:
            status = service->programmer->write(
                service->programmer->context,
                service->request.target_address + service->source_offset,
                service->write_buffer, service->pending_size);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(service, status, BOOT_ERROR_SECONDARY_MCU_PROGRAM);
                break;
            }
            service->target_may_be_modified = 1;
            service->stage                  = SECONDARY_MCU_UPDATE_STAGE_READBACK;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_READBACK:
            status = service->programmer->read(
                service->programmer->context,
                service->request.target_address + service->source_offset,
                service->readback_buffer, service->pending_size);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(service->write_buffer, service->readback_buffer,
                        service->pending_size) != 0))
            {
                BeginFailure(service,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_SECONDARY_MCU_VERIFY);
                break;
            }
            service->source_offset += service->pending_size;
            service->pending_size = 0U;
            service->stage        = SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_END:
            status = service->programmer->end(service->programmer->context);
            if (!FirmwareStatus_IsOk(status))
            {
                /* end 失败时仍允许 abort 做第二次恢复；保留退出错误为根因。 */
                service->failure_status = status;
                service->failure_error = BOOT_ERROR_SECONDARY_MCU_EXIT;
                service->failure_stage = service->stage;
                service->result.status = status;
                service->result.error = BOOT_ERROR_SECONDARY_MCU_EXIT;
                service->result.stage = (uint32_t)service->stage;
                service->stage = SECONDARY_MCU_UPDATE_STAGE_ABORT;
                break;
            }
            service->session_active = 0;
            service->state         = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status = FIRMWARE_STATUS_OK;
            service->result.error  = BOOT_ERROR_NONE;
            service->result.stage  = (uint32_t) service->stage;
            service->stage         = SECONDARY_MCU_UPDATE_STAGE_IDLE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_ABORT:
            ProcessAbort(service);
            break;

        default:
            BeginFailure(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INVALID_STATE);
            break;
    }
}

firmware_status_t SecondaryMcuUpdateService_Cancel(struct secondary_mcu_update_service *service)
{
    if (service == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) ||
        (service->state != SERVICE_RUN_STATE_RUNNING) ||
        (service->target_may_be_modified != 0) ||
        (service->stage == SECONDARY_MCU_UPDATE_STAGE_ABORT))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->cancel_requested = 1;
    if (service->session_active != 0)
    {
        service->stage = SECONDARY_MCU_UPDATE_STAGE_ABORT;
    }
    else
    {
        service->state         = SERVICE_RUN_STATE_CANCELLED;
        service->result.status = FIRMWARE_STATUS_OK;
        service->result.error  = BOOT_ERROR_NONE;
        service->result.stage  = (uint32_t) service->stage;
        service->stage         = SECONDARY_MCU_UPDATE_STAGE_IDLE;
    }
    return FIRMWARE_STATUS_OK;
}

service_run_state_t
SecondaryMcuUpdateService_GetState(const struct secondary_mcu_update_service *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *
SecondaryMcuUpdateService_GetResult(const struct secondary_mcu_update_service *service)
{
    return (service == NULL) ? NULL : &service->result;
}

int SecondaryMcuUpdateService_TargetMayBeModified(
    const struct secondary_mcu_update_service *service)
{
    return (service != NULL) && (service->target_may_be_modified != 0);
}
