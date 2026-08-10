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
        (dependencies->programmer->abort == NULL) || (dependencies->write_buffer == NULL) ||
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
    secondary_mcu_update_service_t *implementation = (secondary_mcu_update_service_t *) service;

    if ((implementation == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) || (implementation->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((request->image_size_bytes == 0U) || (request->target_capacity_bytes == 0U) ||
        (request->image_size_bytes > request->target_capacity_bytes) ||
        (request->erase_page_count == 0U) ||
        (request->target_address > (UINT32_MAX - request->image_size_bytes)) ||
        (request->erase_page_start > (UINT32_MAX - request->erase_page_count)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    implementation->request = *request;
    memset(&implementation->source_info, 0, sizeof(implementation->source_info));
    memset(&implementation->programmer_info, 0, sizeof(implementation->programmer_info));
    implementation->source_offset          = 0U;
    implementation->erase_page_offset      = 0U;
    implementation->pending_size           = 0U;
    implementation->session_active         = 0;
    implementation->target_may_be_modified = 0;
    implementation->cancel_requested       = 0;
    implementation->failure_status         = FIRMWARE_STATUS_OK;
    implementation->failure_error          = BOOT_ERROR_NONE;
    implementation->failure_stage          = SECONDARY_MCU_UPDATE_STAGE_IDLE;
    implementation->result.status          = FIRMWARE_STATUS_OK;
    implementation->result.error           = BOOT_ERROR_NONE;
    implementation->result.stage           = 0U;
    implementation->result.native_error    = 0;
    implementation->state                  = SERVICE_RUN_STATE_RUNNING;
    implementation->stage                  = SECONDARY_MCU_UPDATE_STAGE_SOURCE_INFO;
    return FIRMWARE_STATUS_OK;
}

void SecondaryMcuUpdateService_Process(struct secondary_mcu_update_service *service)
{
    secondary_mcu_update_service_t *implementation = (secondary_mcu_update_service_t *) service;
    firmware_status_t status;
    uint32_t chunk;
    uint32_t remaining;

    if ((implementation == NULL) || (implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (implementation->stage)
    {
        case SECONDARY_MCU_UPDATE_STAGE_SOURCE_INFO:
            status = implementation->source->get_info(implementation->source->context,
                                                      &implementation->source_info);
            if (!FirmwareStatus_IsOk(status) || (implementation->source_info.size_bytes !=
                                                 implementation->request.image_size_bytes))
            {
                BeginFailure(implementation,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_ARGUMENT
                                                         : status,
                             BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            implementation->stage = SECONDARY_MCU_UPDATE_STAGE_BEGIN;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_BEGIN:
            status = implementation->programmer->begin(implementation->programmer->context,
                                                       &implementation->programmer_info);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(implementation, status, BOOT_ERROR_SECONDARY_MCU_ENTER);
                break;
            }
            implementation->session_active = 1;
            status = ValidateProgrammerInfo(&implementation->programmer_info);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(implementation, status, BOOT_ERROR_SECONDARY_MCU_TARGET);
                break;
            }
            implementation->stage = SECONDARY_MCU_UPDATE_STAGE_ERASE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_ERASE:
            remaining =
                implementation->request.erase_page_count - implementation->erase_page_offset;
            if (remaining == 0U)
            {
                implementation->stage = SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE;
                break;
            }
            chunk  = MinU32(remaining, implementation->programmer_info.max_erase_block_count);
            status = implementation->programmer->erase(implementation->programmer->context,
                                                       implementation->request.erase_page_start +
                                                           implementation->erase_page_offset,
                                                       chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(implementation, status, BOOT_ERROR_SECONDARY_MCU_ERASE);
                break;
            }
            implementation->target_may_be_modified = 1;
            implementation->erase_page_offset += chunk;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE:
            remaining = implementation->request.image_size_bytes - implementation->source_offset;
            if (remaining == 0U)
            {
                implementation->stage = SECONDARY_MCU_UPDATE_STAGE_END;
                break;
            }
            chunk  = MinU32(remaining, implementation->buffer_size);
            chunk  = MinU32(chunk, implementation->programmer_info.max_write_size);
            chunk  = MinU32(chunk, implementation->programmer_info.max_read_size);
            status = implementation->source->read(implementation->source->context,
                                                  implementation->source_offset,
                                                  implementation->write_buffer, chunk);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(implementation, status, BOOT_ERROR_SECONDARY_MCU_SOURCE);
                break;
            }
            implementation->pending_size = chunk;
            implementation->stage        = SECONDARY_MCU_UPDATE_STAGE_WRITE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_WRITE:
            status = implementation->programmer->write(
                implementation->programmer->context,
                implementation->request.target_address + implementation->source_offset,
                implementation->write_buffer, implementation->pending_size);
            if (!FirmwareStatus_IsOk(status))
            {
                BeginFailure(implementation, status, BOOT_ERROR_SECONDARY_MCU_PROGRAM);
                break;
            }
            implementation->target_may_be_modified = 1;
            implementation->stage                  = SECONDARY_MCU_UPDATE_STAGE_READBACK;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_READBACK:
            status = implementation->programmer->read(
                implementation->programmer->context,
                implementation->request.target_address + implementation->source_offset,
                implementation->readback_buffer, implementation->pending_size);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(implementation->write_buffer, implementation->readback_buffer,
                        implementation->pending_size) != 0))
            {
                BeginFailure(implementation,
                             FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                             BOOT_ERROR_SECONDARY_MCU_VERIFY);
                break;
            }
            implementation->source_offset += implementation->pending_size;
            implementation->pending_size = 0U;
            implementation->stage        = SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_END:
            status = implementation->programmer->end(implementation->programmer->context);
            if (!FirmwareStatus_IsOk(status))
            {
                /* end 失败时仍允许 abort 做第二次恢复；保留退出错误为根因。 */
                implementation->failure_status = status;
                implementation->failure_error = BOOT_ERROR_SECONDARY_MCU_EXIT;
                implementation->failure_stage = implementation->stage;
                implementation->result.status = status;
                implementation->result.error = BOOT_ERROR_SECONDARY_MCU_EXIT;
                implementation->result.stage = (uint32_t)implementation->stage;
                implementation->stage = SECONDARY_MCU_UPDATE_STAGE_ABORT;
                break;
            }
            implementation->session_active = 0;
            implementation->state         = SERVICE_RUN_STATE_SUCCEEDED;
            implementation->result.status = FIRMWARE_STATUS_OK;
            implementation->result.error  = BOOT_ERROR_NONE;
            implementation->result.stage  = (uint32_t) implementation->stage;
            implementation->stage         = SECONDARY_MCU_UPDATE_STAGE_IDLE;
            break;

        case SECONDARY_MCU_UPDATE_STAGE_ABORT:
            ProcessAbort(implementation);
            break;

        default:
            BeginFailure(implementation, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INVALID_STATE);
            break;
    }
}

firmware_status_t SecondaryMcuUpdateService_Cancel(struct secondary_mcu_update_service *service)
{
    secondary_mcu_update_service_t *implementation = (secondary_mcu_update_service_t *) service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING) ||
        (implementation->target_may_be_modified != 0) ||
        (implementation->stage == SECONDARY_MCU_UPDATE_STAGE_ABORT))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    implementation->cancel_requested = 1;
    if (implementation->session_active != 0)
    {
        implementation->stage = SECONDARY_MCU_UPDATE_STAGE_ABORT;
    }
    else
    {
        implementation->state         = SERVICE_RUN_STATE_CANCELLED;
        implementation->result.status = FIRMWARE_STATUS_OK;
        implementation->result.error  = BOOT_ERROR_NONE;
        implementation->result.stage  = (uint32_t) implementation->stage;
        implementation->stage         = SECONDARY_MCU_UPDATE_STAGE_IDLE;
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
