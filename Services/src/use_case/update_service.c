/**
 * @file update_service.c
 * @brief 按请求选择安装 APP/GUI Runtime 的增量安装器。
 *
 * 安装遵循“先验证全部选中源文件、后首次擦除”的原则：Manifest 和请求选择的
 * APP/GUI 都完成长度及 SHA-256 校验后，才允许修改外部 Flash。每个 Process 调用只推进一次
 * 有界读写、哈希、异步轮询或状态转换，以便由 Application 主循环安全驱动。
 */
#include "services/use_case/update_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/capability/update_request_service_api.h"

/**
 * @brief 将内部阶段转换为日志文字。
 *
 * 阶段枚举不属于公开 API；字符串只用于现场诊断，不能作为业务逻辑输入。
 */
static const char *UpdateStageName(update_stage_t stage)
{
    switch (stage)
    {
        case UPDATE_STAGE_SOURCE_APP_OPEN:
            return "source-app   -open";
        case UPDATE_STAGE_SOURCE_APP_SIZE:
            return "source-app   -size";
        case UPDATE_STAGE_SOURCE_APP_HASH:
            return "source-app   -hash";
        case UPDATE_STAGE_SOURCE_APP_VERIFY:
            return "source-app -verify";
        case UPDATE_STAGE_SOURCE_GUI_OPEN:
            return "source-gui   -open";
        case UPDATE_STAGE_SOURCE_GUI_SIZE:
            return "source-gui   -size";
        case UPDATE_STAGE_SOURCE_GUI_HASH:
            return "source-gui   -hash";
        case UPDATE_STAGE_SOURCE_GUI_VERIFY:
            return "source-gui -verify";
        case UPDATE_STAGE_XIP_CHECK_INDIRECT:
            return "xip-check-indirect";
        case UPDATE_STAGE_XIP_EXIT:
            return "xip-exit";
        case UPDATE_STAGE_XIP_VERIFY_INDIRECT:
            return "xip-verify-indirect";
        case UPDATE_STAGE_APP_ERASE:
            return "app-erase";
        case UPDATE_STAGE_APP_ERASE_POLL:
            return "app-erase   -poll";
        case UPDATE_STAGE_APP_PROGRAM_OPEN:
            return "app-program -open";
        case UPDATE_STAGE_APP_PROGRAM_READ:
            return "app-program -read";
        case UPDATE_STAGE_APP_PROGRAM_START:
            return "app-program -start";
        case UPDATE_STAGE_APP_PROGRAM_POLL:
            return "app-program -poll";
        case UPDATE_STAGE_APP_PROGRAM_HASH:
            return "app-program -hash";
        case UPDATE_STAGE_APP_TARGET_READ:
            return "app-target  -read";
        case UPDATE_STAGE_APP_TARGET_HASH:
            return "app-target  -hash";
        case UPDATE_STAGE_GUI_ERASE:
            return "gui-erase";
        case UPDATE_STAGE_GUI_ERASE_POLL:
            return "gui-erase   -poll";
        case UPDATE_STAGE_GUI_PROGRAM_OPEN:
            return "gui-program -open";
        case UPDATE_STAGE_GUI_PROGRAM_READ:
            return "gui-program -read";
        case UPDATE_STAGE_GUI_PROGRAM_START:
            return "gui-program-start";
        case UPDATE_STAGE_GUI_PROGRAM_POLL:
            return "gui-program -poll";
        case UPDATE_STAGE_GUI_PROGRAM_HASH:
            return "gui-program -hash";
        case UPDATE_STAGE_GUI_TARGET_READ:
            return "gui-target  -read";
        case UPDATE_STAGE_GUI_TARGET_HASH:
            return "gui-target  -hash";
        case UPDATE_STAGE_BUILD_RECORD_CANDIDATE:
            return "build-record-candidate";
        case UPDATE_STAGE_FAILURE_CLOSE:
            return "failure-close";
        case UPDATE_STAGE_CANCEL_CLOSE:
            return "cancel-close";
        case UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT:
            return "cancel-xip-check-indirect";
        case UPDATE_STAGE_CANCEL_XIP_EXIT:
            return "cancel-xip-exit";
        case UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT:
            return "cancel-xip-verify-indirect";
        default:
            return "other";
    }
}

/** 返回两个无符号 32 位数中的较小值，用于限制单次 I/O 长度。 */
static uint32_t MinU32(uint32_t left, uint32_t right)
{
    return (left < right) ? left : right;
}

/** 返回该阶段是否会在逐页编程或逐扇区擦除时高频往返。 */
static int IsHighFrequencyStage(update_stage_t stage)
{
    switch (stage)
    {
        case UPDATE_STAGE_APP_ERASE:
        case UPDATE_STAGE_APP_ERASE_POLL:
        case UPDATE_STAGE_APP_PROGRAM_READ:
        case UPDATE_STAGE_APP_PROGRAM_START:
        case UPDATE_STAGE_APP_PROGRAM_POLL:
        case UPDATE_STAGE_GUI_ERASE:
        case UPDATE_STAGE_GUI_ERASE_POLL:
        case UPDATE_STAGE_GUI_PROGRAM_READ:
        case UPDATE_STAGE_GUI_PROGRAM_START:
        case UPDATE_STAGE_GUI_PROGRAM_POLL:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief 终止当前 Prepare 或 Install，并记录失败结果。
 *
 * 若发布源文件仍被服务打开，服务会先进入独立的关闭恢复阶段；只有 close 成功
 * 或重试耗尽后才进入 FAILED。结果中的 stage 保留触发失败的内部阶段，避免清理
 * 阶段掩盖根因；Application 只需读取公开生命周期。
 */
static void FinalizeFailure(update_service_t *service, firmware_status_t cleanup_status)
{
    service->result.status = service->failure_status;
    service->result.error  = service->failure_error;
    service->result.stage  = (uint32_t) service->failure_stage;
    service->result.native_error =
        FirmwareStatus_IsOk(cleanup_status) ? 0 : (int32_t) cleanup_status;
    service->state           = SERVICE_RUN_STATE_FAILED;
    service->candidate_ready = 0;
}

/**
 * @brief 完成已经清理干净的取消请求。
 *
 * CANCELLED 表示文件所有权已释放；若安装曾进入 XIP 保护区，也已通过 Adapter
 * 确认回到 indirect 模式。结果快照记录最后的清理阶段，方便现场诊断。
 */
static void FinalizeCancellation(update_service_t *service)
{
    service->result.status            = FIRMWARE_STATUS_OK;
    service->result.error             = BOOT_ERROR_NONE;
    service->result.stage             = (uint32_t) service->stage;
    service->result.native_error      = 0;
    service->candidate_ready          = 0;
    service->cancel_requires_indirect = 0;
    service->state                    = SERVICE_RUN_STATE_CANCELLED;
    service->stage                    = UPDATE_STAGE_IDLE;
}

/** 将取消期间无法恢复的清理错误转换为可观察的 FAILED 结果。 */
static void FinalizeCancellationFailure(update_service_t *service, firmware_status_t status,
                                        boot_error_t error)
{
    service->failure_status = status;
    service->failure_error  = error;
    service->failure_stage  = service->stage;
    FinalizeFailure(service, status);
}

/**
 * @brief 记录首个失败，并在必要时把文件关闭恢复交给后续 Process 调用。
 *
 * 文件仍被服务占用时不能立即宣称失败已清理完成。保持 RUNNING 使 Application
 * 继续驱动服务，直到真实 close 成功或有限重试耗尽。
 */
static void Fail(update_service_t *service, firmware_status_t status, boot_error_t error)
{
    service->failure_status  = status;
    service->failure_error   = error;
    service->failure_stage   = service->stage;
    service->candidate_ready = 0;
    if (service->source_file_open != 0)
    {
        service->close_retry_count = 0U;
        service->stage             = UPDATE_STAGE_FAILURE_CLOSE;
        return;
    }
    FinalizeFailure(service, FIRMWARE_STATUS_OK);
}

/** 重置本次源或目标数据流的哈希上下文。 */
static firmware_status_t HashReset(update_service_t *service)
{
    return service->hash->reset(service->hash->context);
}

/** 向当前哈希上下文追加一个已成功读取的数据块。 */
static firmware_status_t HashUpdate(update_service_t *service, const void *data, uint32_t size)
{
    return service->hash->update(service->hash->context, data, size);
}

/** 完成当前哈希上下文，并将 32 字节 SHA-256 写入调用者缓冲区。 */
static firmware_status_t HashFinish(update_service_t *service, uint8_t digest[32U])
{
    return service->hash->finish(service->hash->context, digest);
}

/**
 * @brief 关闭当前发布源文件，并仅在底层 close 成功后更新所有权标志。
 *
 * source_file_open 表示真实的服务层文件占用状态，不能因一次失败而提前清零。
 */
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

/**
 * @brief 在主操作失败后重试关闭发布源文件。
 *
 * 当 close 最终成功时，服务才公布原始操作失败；当有限重试全部失败时，真实的
 * source_file_open 状态保持为真，供上层卸载失败和 fail-closed 路径准确反映。
 */
static void ProcessFailureClose(update_service_t *service)
{
    firmware_status_t status = CloseSource(service);

    if (FirmwareStatus_IsOk(status))
    {
        FinalizeFailure(service, FIRMWARE_STATUS_OK);
        return;
    }

    ++service->close_retry_count;
    LOG_WARN("update", "source close retry %lu/%u failed: status=%d",
             (unsigned long) service->close_retry_count,
             (unsigned) UPDATE_SERVICE_CLOSE_RETRY_LIMIT, (int) status);
    if (service->close_retry_count >= UPDATE_SERVICE_CLOSE_RETRY_LIMIT)
    {
        FinalizeFailure(service, status);
    }
}

/**
 * @brief 在取消时进入下一项真实清理工作，或在所有权均已释放后公布 CANCELLED。
 */
static void ContinueCancellationCleanup(update_service_t *service)
{
    if (service->cancel_requires_indirect != 0)
    {
        service->xip_exit_attempts = 0U;
        service->stage             = UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT;
        return;
    }
    FinalizeCancellation(service);
}

/**
 * @brief 重试关闭取消请求遗留的发布文件。
 *
 * close 连续失败时，服务仍保留 source_file_open，且以 FAILED 收敛，避免之后的
 * Process 重新回到原安装阶段并继续破坏性操作。
 */
static void ProcessCancelClose(update_service_t *service)
{
    firmware_status_t status = CloseSource(service);

    if (FirmwareStatus_IsOk(status))
    {
        ContinueCancellationCleanup(service);
        return;
    }

    ++service->close_retry_count;
    LOG_WARN("update", "cancel source close retry %lu/%u failed: status=%d",
             (unsigned long) service->close_retry_count,
             (unsigned) UPDATE_SERVICE_CLOSE_RETRY_LIMIT, (int) status);
    if (service->close_retry_count >= UPDATE_SERVICE_CLOSE_RETRY_LIMIT)
    {
        FinalizeCancellationFailure(service, status, BOOT_ERROR_INTERNAL);
    }
}

/**
 * @brief 验证注入的固定布局是否与发布合同及实际 Flash 几何兼容。
 *
 * 除检查 APP/GUI 地址常量外，还确保所有分区边界按擦除粒度对齐且不越过设备容量。
 */
static firmware_status_t ValidateRuntimeLayout(const async_block_device_info_t *storage_info,
                                               const boot_runtime_layout_t *layout)
{
    uint32_t end;

    if ((storage_info == NULL) || (layout == NULL) || (storage_info->capacity_bytes == 0U) ||
        (storage_info->program_size == 0U) || (storage_info->erase_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 禁止用“看似合法”的可注入布局改变冻结的量产分区合同。 */
    if ((layout->app_offset != BOOT_APP_FLASH_OFFSET) ||
        (layout->app_xip_base != BOOT_APP_RUNTIME_BASE) ||
        (layout->app_max_size != BOOT_APP_RUNTIME_SIZE) ||
        (layout->gui_offset != BOOT_GUI_FLASH_OFFSET) ||
        (layout->gui_mmap_base != BOOT_GUI_RUNTIME_BASE) ||
        (layout->gui_max_size != BOOT_GUI_RUNTIME_SIZE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* 擦除范围必须按器件粒度对齐，并完整落在物理容量内。 */
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

/**
 * @brief 取得当前已打开源文件的长度，并与 Manifest 和分区容量交叉校验。
 *
 * 文件长度为零、超过分区或不等于 Manifest 声明值都会被拒绝。
 */
static firmware_status_t GetSourceSize(update_service_t *service, uint32_t expected,
                                       uint32_t maximum, uint32_t *actual)
{
    firmware_status_t status =
        service->package_source->get_size(service->package_source->context, actual);

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

/**
 * @brief 按当前擦除偏移启动一个 Flash 擦除单元。
 *
 * 调用者负责随后轮询操作结果；达到分区末尾时不触发底层 I/O。
 */
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

/** 轮询异步块设备，并读取当前操作结果快照。 */
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

/**
 * @brief 计算下一次页编程的安全块大小。
 *
 * 块不会跨越 Flash program_size 边界、I/O 缓冲区或当前 Runtime 分区末尾。
 */
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
    remaining      = service->active_source_size - service->program_offset;
    page_remaining = service->storage_info.program_size -
                     (service->program_offset % service->storage_info.program_size);
    *chunk_size    = MinU32(remaining, MinU32(service->io_buffer_size, page_remaining));
    if ((*chunk_size == 0U) || (service->program_offset > runtime_size) ||
        (*chunk_size > (runtime_size - service->program_offset)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    return FIRMWARE_STATUS_OK;
}

/**
 * @brief 打开当前 APP/GUI 源文件，并初始化其流式写入上下文。
 *
 * 在任何程序操作之前再次检查源文件长度，避免安装前后文件被替换或截断。
 */
static firmware_status_t StartProgramSource(update_service_t *service, package_file_id_t file,
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
    status =
        GetSourceSize(service, component->size_bytes, runtime_size, &service->active_source_size);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* 保留当前组件和固定目标区域，后续阶段无需重新根据 app/gui 分支取值。 */
    service->active_component      = component;
    service->active_runtime_offset = runtime_offset;
    service->active_runtime_size   = runtime_size;
    service->program_offset        = 0U;
    return HashReset(service);
}

/**
 * @brief 在 XIP 间接模式确认完成后进入源预检或实际擦除。
 *
 * 初次检查只保护整个安装会话的起点；APP/GUI 预检结束后会再次检查，以覆盖其他
 * 串行使用者在较长预检窗口中重新进入 memory-mapped 模式的情况。
 */
static void ContinueAfterIndirectConfirmation(update_service_t *service)
{
    if (service->xip_check_before_runtime_mutation != 0)
    {
        service->xip_check_before_runtime_mutation = 0;
        service->stage = ((service->request.component_mask & UPDATE_COMPONENT_APP) != 0U)
                             ? UPDATE_STAGE_APP_ERASE
                             : UPDATE_STAGE_GUI_ERASE;
    }
    else
    {
        service->stage = ((service->request.component_mask & UPDATE_COMPONENT_APP) != 0U)
                             ? UPDATE_STAGE_SOURCE_APP_OPEN
                             : UPDATE_STAGE_SOURCE_GUI_OPEN;
    }
}

/**
 * @brief 在首次擦除及真正开始擦除前确认 QSPI 已处于 indirect 模式。
 *
 * Update Service 是唯一执行 Runtime 擦写的上层所有者，因此它必须处理已遗留的
 * memory-mapped 状态。先查询，再退出，再查询后置条件；任何一步查询失败都会在不
 * 修改 Runtime 的前提下终止安装。Abort 调用的返回值并不等同于硬件后置状态：即使
 * 返回超时或 I/O 错误，也必须查询一次，只有确认仍映射时才继续有限重试。
 */
static void ProcessEnsureIndirectMode(update_service_t *service)
{
    firmware_status_t status;
    int mapped = 0;

    switch (service->stage)
    {
        case UPDATE_STAGE_XIP_CHECK_INDIRECT:
            status = service->xip_controller->is_memory_mapped(service->xip_controller->context,
                                                               &mapped);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_XIP_SETUP);
            }
            else if (mapped == 0)
            {
                /* 已是 indirect 模式，无需无条件 Abort，直接进入源文件预检。 */
                ContinueAfterIndirectConfirmation(service);
            }
            else
            {
                service->stage = UPDATE_STAGE_XIP_EXIT;
            }
            break;

        case UPDATE_STAGE_XIP_EXIT:
            status = service->xip_controller->exit_memory_mapped(service->xip_controller->context);
            ++service->xip_exit_attempts;
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("update", "xip exit attempt %lu/%u returned status=%d",
                         (unsigned long) service->xip_exit_attempts,
                         (unsigned) UPDATE_SERVICE_XIP_EXIT_RETRY_LIMIT, (int) status);
            }
            /* HAL Abort 可能在完成硬件退出后才报告超时；后置查询才是事实依据。 */
            service->stage = UPDATE_STAGE_XIP_VERIFY_INDIRECT;
            break;

        case UPDATE_STAGE_XIP_VERIFY_INDIRECT:
            status = service->xip_controller->is_memory_mapped(service->xip_controller->context,
                                                               &mapped);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_XIP_SETUP);
            }
            else if (mapped != 0)
            {
                /* Adapter 报告仍映射时重新 Abort；达到上限后禁止任何擦写。 */
                if (service->xip_exit_attempts >= UPDATE_SERVICE_XIP_EXIT_RETRY_LIMIT)
                {
                    Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_XIP_SETUP);
                }
                else
                {
                    service->stage = UPDATE_STAGE_XIP_EXIT;
                }
            }
            else
            {
                LOG_INFO("update", "xip confirmed indirect before runtime mutation");
                ContinueAfterIndirectConfirmation(service);
            }
            break;

        default:
            Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_XIP_SETUP);
            break;
    }
}

/**
 * @brief 在取消安装时将 QSPI 收敛到已确认的 indirect 模式。
 *
 * 此路径和正常安装一样以查询结果为准。这样即便 Abort 返回错误，只要硬件已完成
 * 退出，取消仍可安全完成；反之即使调用返回成功但仍映射，也绝不报告 CANCELLED。
 */
static void ProcessCancelEnsureIndirectMode(update_service_t *service)
{
    firmware_status_t status;
    int mapped = 0;

    switch (service->stage)
    {
        case UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT:
            status = service->xip_controller->is_memory_mapped(service->xip_controller->context,
                                                               &mapped);
            if (!FirmwareStatus_IsOk(status))
            {
                FinalizeCancellationFailure(service, status, BOOT_ERROR_XIP_SETUP);
            }
            else if (mapped == 0)
            {
                FinalizeCancellation(service);
            }
            else
            {
                service->stage = UPDATE_STAGE_CANCEL_XIP_EXIT;
            }
            break;

        case UPDATE_STAGE_CANCEL_XIP_EXIT:
            status = service->xip_controller->exit_memory_mapped(service->xip_controller->context);
            ++service->xip_exit_attempts;
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("update", "cancel xip exit attempt %lu/%u returned status=%d",
                         (unsigned long) service->xip_exit_attempts,
                         (unsigned) UPDATE_SERVICE_XIP_EXIT_RETRY_LIMIT, (int) status);
            }
            service->stage = UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT;
            break;

        case UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT:
            status = service->xip_controller->is_memory_mapped(service->xip_controller->context,
                                                               &mapped);
            if (!FirmwareStatus_IsOk(status))
            {
                FinalizeCancellationFailure(service, status, BOOT_ERROR_XIP_SETUP);
            }
            else if (mapped == 0)
            {
                FinalizeCancellation(service);
            }
            else if (service->xip_exit_attempts >= UPDATE_SERVICE_XIP_EXIT_RETRY_LIMIT)
            {
                FinalizeCancellationFailure(service, FIRMWARE_STATUS_INVALID_STATE,
                                            BOOT_ERROR_XIP_SETUP);
            }
            else
            {
                service->stage = UPDATE_STAGE_CANCEL_XIP_EXIT;
            }
            break;

        default:
            FinalizeCancellationFailure(service, FIRMWARE_STATUS_INVALID_STATE,
                                        BOOT_ERROR_XIP_SETUP);
            break;
    }
}

firmware_status_t UpdateService_Init(update_service_t *service,
                                     const update_service_dependencies_t *dependencies)
{
    firmware_status_t status;
    async_block_device_info_t info;

    /* 先验证顶层依赖对象，再验证每个服务实际会调用的回调。 */
    if ((service == NULL) || (dependencies == NULL) || (dependencies->package_source == NULL) ||
        (dependencies->manifest_service == NULL) ||
        (dependencies->update_request_service == NULL) || (dependencies->hash == NULL) ||
        (dependencies->clock == NULL) ||
        (dependencies->storage == NULL) || (dependencies->xip_controller == NULL) ||
        (dependencies->runtime_layout == NULL) || (dependencies->manifest_buffer == NULL) ||
        (dependencies->io_buffer == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((dependencies->package_source->open == NULL) ||
        (dependencies->package_source->close == NULL) ||
        (dependencies->package_source->get_size == NULL) ||
        (dependencies->package_source->read_at == NULL) || (dependencies->hash->reset == NULL) ||
        (dependencies->hash->update == NULL) || (dependencies->hash->finish == NULL) ||
        (dependencies->clock->now_ms == NULL) ||
        (dependencies->storage->get_info == NULL) || (dependencies->storage->read == NULL) ||
        (dependencies->storage->program_start == NULL) ||
        (dependencies->storage->erase_start == NULL) || (dependencies->storage->poll == NULL) ||
        (dependencies->storage->get_operation_result == NULL) ||
        (dependencies->xip_controller->is_memory_mapped == NULL) ||
        (dependencies->xip_controller->exit_memory_mapped == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* Flash 几何在 Init 时读取一次，并作为本实例的安装约束冻结。 */
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
    /* Manifest 需要完整驻留，流式缓冲区至少能容纳一个 Flash program 单元。 */
    if ((dependencies->manifest_buffer_size < MANIFEST_SERVICE_MAX_DOCUMENT_SIZE) ||
        (dependencies->io_buffer_size < info.program_size))
    {
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }

    /* 仅在所有验证通过后清零并发布已初始化状态，失败不会破坏调用者对象。 */
    memset(service, 0, sizeof(*service));
    service->package_source         = dependencies->package_source;
    service->manifest_service       = dependencies->manifest_service;
    service->update_request_service = dependencies->update_request_service;
    service->hash                   = dependencies->hash;
    service->clock                  = dependencies->clock;
    service->storage                = dependencies->storage;
    service->xip_controller         = dependencies->xip_controller;
    service->runtime_layout         = dependencies->runtime_layout;
    service->storage_info           = info;
    service->manifest_buffer        = dependencies->manifest_buffer;
    service->manifest_buffer_size   = dependencies->manifest_buffer_size;
    service->io_buffer              = dependencies->io_buffer;
    service->io_buffer_size         = dependencies->io_buffer_size;
    service->state                  = SERVICE_RUN_STATE_IDLE;
    service->stage                  = UPDATE_STAGE_IDLE;
    service->initialized            = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_PrepareStart(struct update_service *service,
                                             const update_request_t *request)
{
    update_service_t *implementation = (update_service_t *) service;

    if ((implementation == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 同一实例必须完成或被复位后才能接受新的 trusted request。 */
    if ((implementation->initialized == 0) || (implementation->state != SERVICE_RUN_STATE_IDLE) ||
        (implementation->source_file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* 复制请求和清空上次输出，防止上一次安装残留影响本次策略。 */
    implementation->request = *request;
    if (implementation->request.component_mask == 0U)
    {
        /* Compatibility for in-process V1 callers predating explicit selection. */
        implementation->request.component_mask = UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
    }
    memset(&implementation->manifest, 0, sizeof(implementation->manifest));
    memset(&implementation->candidate_record, 0, sizeof(implementation->candidate_record));
    implementation->manifest_ready                    = 0;
    implementation->candidate_ready                   = 0;
    implementation->runtime_may_be_modified           = 0;
    implementation->failure_status                    = FIRMWARE_STATUS_OK;
    implementation->failure_error                     = BOOT_ERROR_NONE;
    implementation->failure_stage                     = UPDATE_STAGE_IDLE;
    implementation->close_retry_count                 = 0U;
    implementation->xip_exit_attempts                 = 0U;
    implementation->xip_check_before_runtime_mutation = 0;
    implementation->cancel_requires_indirect          = 0;
    implementation->progress_last_percent             = 0U;
    implementation->progress_last_log_ms              = 0U;
    implementation->progress_tracking_started         = 0;
    implementation->manifest_size                     = 0U;
    implementation->manifest_offset                   = 0U;
    implementation->stage                             = UPDATE_STAGE_PREPARE_MANIFEST_OPEN;
    implementation->state                             = SERVICE_RUN_STATE_RUNNING;
    implementation->result.status                     = FIRMWARE_STATUS_OK;
    implementation->result.error                      = BOOT_ERROR_NONE;
    implementation->result.stage                      = (uint32_t) implementation->stage;
    implementation->result.native_error               = 0;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStartWithRecord(
    struct update_service *service,
    const boot_active_record_t *current_record)
{
    update_service_t *implementation = (update_service_t *) service;
    uint32_t host_mask;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* Install 只能消费刚刚成功 Prepare 的 Manifest，不能绕过源校验。 */
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_SUCCEEDED) ||
        (implementation->stage != UPDATE_STAGE_PREPARED) || (implementation->manifest_ready == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    host_mask = implementation->request.component_mask &
                (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI);
    if ((host_mask == 0U) ||
        ((host_mask != (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI)) &&
         (current_record == NULL)))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (current_record != NULL)
    {
        implementation->base_record = *current_record;
        if (implementation->base_record.component_mask == 0U)
        {
            implementation->base_record.component_mask =
                UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
        }
        implementation->base_record_valid = 1;
    }
    else
    {
        memset(&implementation->base_record, 0, sizeof(implementation->base_record));
        implementation->base_record_valid = 0;
    }
    implementation->candidate_ready         = 0;
    implementation->runtime_may_be_modified = 0;
    if (implementation->source_file_open != 0)
    {
        /* Install 不得覆盖仍被上一次流程持有的真实文件状态。 */
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    implementation->xip_exit_attempts                 = 0U;
    implementation->xip_check_before_runtime_mutation = 0;
    implementation->cancel_requires_indirect          = 0;
    implementation->progress_last_percent             = 0U;
    implementation->progress_last_log_ms =
        implementation->clock->now_ms(implementation->clock->context);
    implementation->progress_tracking_started = 1;
    implementation->stage                             = UPDATE_STAGE_XIP_CHECK_INDIRECT;
    implementation->state                             = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t UpdateService_InstallStart(struct update_service *service)
{
    return UpdateService_InstallStartWithRecord(service, NULL);
}

static void ProcessPrepare(update_service_t *service)
{
    firmware_status_t status;
    uint32_t chunk;
    uint32_t bytes_read;

    switch (service->stage)
    {
        case UPDATE_STAGE_PREPARE_MANIFEST_OPEN:
            /* 固定文件名由 PackageSource Adapter 决定，Service 不接触路径字符串。 */
            status = service->package_source->open(service->package_source->context,
                                                   PACKAGE_FILE_MANIFEST);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_PREPARE);
                break;
            }
            service->source_file_open = 1;
            service->stage            = UPDATE_STAGE_PREPARE_MANIFEST_SIZE;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_SIZE:
            /* 在读取前限制长度，避免把超大或空 Manifest 拷入静态缓冲区。 */
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
            service->stage           = UPDATE_STAGE_PREPARE_MANIFEST_READ;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_READ:
            /* 每次只搬运一块，直到整个 Manifest 已连续填充到专用缓冲区。 */
            chunk =
                MinU32(service->io_buffer_size, service->manifest_size - service->manifest_offset);
            bytes_read = 0U;
            status     = service->package_source->read_at(
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
            /* 解析前先释放文件，保证后续 APP/GUI 打开及卷卸载不会发生重入。 */
            status = CloseSource(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_PREPARE);
                break;
            }
            service->stage = UPDATE_STAGE_PREPARE_MANIFEST_PARSE;
            break;
        case UPDATE_STAGE_PREPARE_MANIFEST_PARSE:
            /* 先检查 Manifest 自身，再验证它与 trusted request 的绑定关系。 */
            status = ManifestService_ParseAndValidate(service->manifest_service,
                                                      service->manifest_buffer,
                                                      service->manifest_size, &service->manifest);
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
            /* Prepare 成功只表示来源可信且完整；版本策略仍由 Application 决定。 */
            service->manifest_ready = 1;
            service->stage          = UPDATE_STAGE_PREPARED;
            service->state          = SERVICE_RUN_STATE_SUCCEEDED;
            break;
        default:
            break;
    }
}

/**
 * @brief 对 APP 或 GUI 源文件执行安装前的完整性预检。
 *
 * 同一状态机实现复用 APP/GUI 两种组件：先打开并确认精确长度，再流式计算摘要，
 * 校验通过后关闭文件。所有被请求选择的组件通过预检后，流程才进入首次擦除。
 */
static void ProcessSourceHash(update_service_t *service, package_file_id_t file,
                              const manifest_app_component_t *component, uint32_t maximum_size,
                              update_stage_t size_stage, update_stage_t hash_stage,
                              update_stage_t verify_stage, boot_error_t size_error,
                              boot_error_t hash_error)
{
    firmware_status_t status;
    uint32_t bytes_read;
    uint32_t chunk;

    switch (service->stage)
    {
        case UPDATE_STAGE_SOURCE_APP_OPEN:
        case UPDATE_STAGE_SOURCE_GUI_OPEN:
            /* 每次仅允许打开一个固定 payload，避免 FatFs 卷上同时持有多个文件。 */
            status = service->package_source->open(service->package_source->context, file);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->source_file_open = 1;
            service->stage            = size_stage;
            break;
        case UPDATE_STAGE_SOURCE_APP_SIZE:
        case UPDATE_STAGE_SOURCE_GUI_SIZE:
            /* 文件长度必须同时等于 Manifest 和对应固定 Runtime 分区约束。 */
            status = GetSourceSize(service, component->size_bytes, maximum_size,
                                   &service->active_source_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_OUT_OF_RANGE : status,
                     size_error);
                break;
            }
            service->source_offset = 0U;
            status                 = HashReset(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, hash_error);
                break;
            }
            service->stage = hash_stage;
            break;
        case UPDATE_STAGE_SOURCE_APP_HASH:
        case UPDATE_STAGE_SOURCE_GUI_HASH:
            /* 预检哈希直接覆盖 io_buffer；该阶段尚未发生任何 Flash 擦写。 */
            chunk      = MinU32(service->io_buffer_size,
                                service->active_source_size - service->source_offset);
            bytes_read = 0U;
            status = service->package_source->read_at(service->package_source->context,
                                                      service->source_offset, service->io_buffer,
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
                /* 最后一块写入后一次性完成 SHA-256，避免以中间状态做比较。 */
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
            /* 摘要完全一致后才释放该文件并转到下一组件或首个擦除阶段。 */
            if (memcmp(service->source_digest, component->sha256, FIRMWARE_SHA256_DIGEST_SIZE) != 0)
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
            /* 只预检请求选择的文件；最后一个源通过后才允许进入破坏性操作。 */
            service->stage = ((file == PACKAGE_FILE_APP) &&
                              ((service->request.component_mask & UPDATE_COMPONENT_GUI) != 0U))
                                 ? UPDATE_STAGE_SOURCE_GUI_OPEN
                                 : UPDATE_STAGE_XIP_CHECK_INDIRECT;
            if (service->stage == UPDATE_STAGE_XIP_CHECK_INDIRECT)
            {
                /* 预检可能持续较久，真正首个擦除前必须再次确认 XIP 后置状态。 */
                service->xip_check_before_runtime_mutation = 1;
                service->xip_exit_attempts                 = 0U;
                service->erase_offset                      = 0U;
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Calculate the erase length for one payload, rounded to flash sectors.
 *
 * The payload length is trusted only after Prepare has validated the Manifest
 * and the source hash. Rounding is required because the NOR device erases
 * complete sectors, while the final program page may end in the middle of one.
 * Bytes beyond the rounded extent remain outside the committed component size.
 */
static firmware_status_t CalculateEraseLength(const update_service_t *service, int app,
                                              uint32_t *length)
{
    uint32_t image_size;
    uint32_t maximum_size;
    uint32_t device_erase_size;
    uint32_t sector_count;

    if ((service == NULL) || (service->runtime_layout == NULL) || (length == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    image_size = app != 0 ? service->manifest.app.size_bytes : service->manifest.gui.size_bytes;
    maximum_size =
        app != 0 ? service->runtime_layout->app_max_size : service->runtime_layout->gui_max_size;
    device_erase_size = service->storage_info.erase_size;
    if ((image_size == 0U) || (image_size > maximum_size) || (device_erase_size == 0U) ||
        ((maximum_size % device_erase_size) != 0U))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    sector_count = image_size / device_erase_size;
    if ((image_size % device_erase_size) != 0U)
    {
        ++sector_count;
    }
    if (sector_count > (maximum_size / device_erase_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    *length = sector_count * device_erase_size;
    return FIRMWARE_STATUS_OK;
}

/**
 * @brief 计算安装全过程的单调字节进度百分比。
 *
 * 总工作量包含 APP/GUI 源预检、实际擦除、编程和目标回读。擦除长度使用真实的
 * 扇区对齐范围；阶段切换时累计值只前进，不会因 APP/GUI 或子阶段变化归零。
 */
static int CalculateInstallProgressPercent(const update_service_t *service, uint32_t *percent)
{
    uint32_t app_erase_size;
    uint32_t gui_erase_size;
    uint64_t app_size;
    uint64_t gui_size;
    uint64_t completed;
    uint64_t total;

    if ((service == NULL) || (percent == NULL) ||
        !FirmwareStatus_IsOk(CalculateEraseLength(service, 1, &app_erase_size)) ||
        !FirmwareStatus_IsOk(CalculateEraseLength(service, 0, &gui_erase_size)))
    {
        return 0;
    }

    app_size = service->manifest.app.size_bytes;
    gui_size = service->manifest.gui.size_bytes;
    total = (3ULL * app_size) + (3ULL * gui_size) + app_erase_size + gui_erase_size;
    if (total == 0ULL)
    {
        return 0;
    }

    if ((service->stage == UPDATE_STAGE_XIP_CHECK_INDIRECT) ||
        (service->stage == UPDATE_STAGE_XIP_EXIT) ||
        (service->stage == UPDATE_STAGE_XIP_VERIFY_INDIRECT))
    {
        completed = service->xip_check_before_runtime_mutation != 0 ? app_size + gui_size : 0ULL;
    }
    else if ((service->stage >= UPDATE_STAGE_SOURCE_APP_OPEN) &&
             (service->stage <= UPDATE_STAGE_SOURCE_APP_VERIFY))
    {
        completed = MinU32(service->source_offset, (uint32_t) app_size);
    }
    else if ((service->stage >= UPDATE_STAGE_SOURCE_GUI_OPEN) &&
             (service->stage <= UPDATE_STAGE_SOURCE_GUI_VERIFY))
    {
        completed = app_size + MinU32(service->source_offset, (uint32_t) gui_size);
    }
    else if ((service->stage == UPDATE_STAGE_APP_ERASE) ||
             (service->stage == UPDATE_STAGE_APP_ERASE_POLL))
    {
        completed = app_size + gui_size + MinU32(service->erase_offset, app_erase_size);
    }
    else if ((service->stage >= UPDATE_STAGE_APP_PROGRAM_OPEN) &&
             (service->stage <= UPDATE_STAGE_APP_PROGRAM_HASH))
    {
        completed = app_size + gui_size + app_erase_size +
                    MinU32(service->program_offset, (uint32_t) app_size);
    }
    else if ((service->stage == UPDATE_STAGE_APP_TARGET_READ) ||
             (service->stage == UPDATE_STAGE_APP_TARGET_HASH))
    {
        completed = app_size + gui_size + app_erase_size + app_size +
                    MinU32(service->target_offset, (uint32_t) app_size);
    }
    else if ((service->stage == UPDATE_STAGE_GUI_ERASE) ||
             (service->stage == UPDATE_STAGE_GUI_ERASE_POLL))
    {
        completed = (3ULL * app_size) + gui_size + app_erase_size +
                    MinU32(service->erase_offset, gui_erase_size);
    }
    else if ((service->stage >= UPDATE_STAGE_GUI_PROGRAM_OPEN) &&
             (service->stage <= UPDATE_STAGE_GUI_PROGRAM_HASH))
    {
        completed = (3ULL * app_size) + gui_size + app_erase_size + gui_erase_size +
                    MinU32(service->program_offset, (uint32_t) gui_size);
    }
    else if ((service->stage == UPDATE_STAGE_GUI_TARGET_READ) ||
             (service->stage == UPDATE_STAGE_GUI_TARGET_HASH))
    {
        completed = (3ULL * app_size) + (2ULL * gui_size) + app_erase_size + gui_erase_size +
                    MinU32(service->target_offset, (uint32_t) gui_size);
    }
    else if (service->stage == UPDATE_STAGE_BUILD_RECORD_CANDIDATE)
    {
        completed = total;
    }
    else
    {
        return 0;
    }

    if (completed > total)
    {
        completed = total;
    }
    *percent = (uint32_t)((completed * 100ULL) / total);
    return 1;
}

/** 满足“至少前进 1% 且至少间隔 5 秒”时输出当时的实际总进度。 */
static void ReportInstallProgress(update_service_t *service)
{
    uint32_t now_ms;
    uint32_t percent;

    if ((service == NULL) || (service->progress_tracking_started == 0))
    {
        return;
    }
    now_ms = service->clock->now_ms(service->clock->context);
    if ((uint32_t)(now_ms - service->progress_last_log_ms) <
        UPDATE_SERVICE_PROGRESS_LOG_INTERVAL_MS)
    {
        return;
    }
    if ((CalculateInstallProgressPercent(service, &percent) == 0) ||
        (percent < service->progress_last_percent + UPDATE_SERVICE_PROGRESS_LOG_STEP_PERCENT))
    {
        return;
    }

    LOG_INFO("update", "progress=%lu%% stage=%s", (unsigned long) percent,
             UpdateStageName(service->stage));
    service->progress_last_percent = percent;
    service->progress_last_log_ms  = now_ms;
}

/**
 * @brief Drive asynchronous sector erasure for one APP or GUI payload.
 *
 * app non-zero selects APP; otherwise GUI. APP's first erase sets
 * runtime_may_be_modified and keeps it set so Application can fail closed.
 */
static void ProcessErase(update_service_t *service, int app)
{
    uint32_t base =
        app != 0 ? service->runtime_layout->app_offset : service->runtime_layout->gui_offset;
    update_stage_t stage = app != 0 ? UPDATE_STAGE_APP_ERASE : UPDATE_STAGE_GUI_ERASE;
    update_stage_t poll_stage =
        app != 0 ? UPDATE_STAGE_APP_ERASE_POLL : UPDATE_STAGE_GUI_ERASE_POLL;
    update_stage_t next_stage =
        app != 0 ? UPDATE_STAGE_APP_PROGRAM_OPEN : UPDATE_STAGE_GUI_PROGRAM_OPEN;
    async_block_device_operation_result_t operation;
    firmware_status_t status;

    if (service->stage == stage)
    {
        uint32_t size;

        status = CalculateEraseLength(service, app, &size);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("update", "%s erase length invalid: status=%d", app != 0 ? "app" : "gui",
                      (int) status);
            Fail(service, status, app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
            return;
        }
        /* The rounded payload range is erased before opening the source again. */
        if (service->erase_offset >= size)
        {
            service->stage = next_stage;
            return;
        }
        /* 从此刻起任何失败都可能留下不完整 Runtime，标志必须粘滞。 */
        service->runtime_may_be_modified = 1;
        status = StartErase(service, base, size, service->erase_offset);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("update", "%s start failed: status=%d offset=0x%08lx",
                      app != 0 ? "app-erase" : "gui-erase", (int) status,
                      (unsigned long) service->erase_offset);
            Fail(service, status, app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
            return;
        }
        service->stage = poll_stage;
        return;
    }

    /* 启动后由后续 Process 调用轮询，BUSY 不改变阶段和偏移。 */
    status = PollOperation(service, &operation);
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("update", "%s poll failed: status=%d offset=0x%08lx",
                  app != 0 ? "app-erase" : "gui-erase", (int) status,
                  (unsigned long) service->erase_offset);
        Fail(service, status, app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
    }
    else if (operation.state == ASYNC_BLOCK_DEVICE_OPERATION_BUSY)
    {
        return;
    }
    else if ((operation.state != ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED) ||
             !FirmwareStatus_IsOk(operation.status))
    {
        LOG_ERROR("update", "%s operation failed: state=%d status=%d offset=0x%08lx",
                  app != 0 ? "app-erase" : "gui-erase", (int) operation.state,
                  (int) operation.status, (unsigned long) service->erase_offset);
        Fail(service,
             FirmwareStatus_IsOk(operation.status) ? FIRMWARE_STATUS_IO_ERROR : operation.status,
             app != 0 ? BOOT_ERROR_APP_ERASE : BOOT_ERROR_GUI_ERASE);
    }
    else
    {
        /* 本擦除单元确认成功后，移动到下一个按几何粒度对齐的偏移。 */
        service->erase_offset += service->storage_info.erase_size;
        service->stage = stage;
    }
}

/**
 * @brief 驱动 APP 或 GUI 的“读源文件、页编程、源哈希、关闭文件”流程。
 *
 * app 非零时处理 APP；否则处理 GUI。读取块严格限制在 Flash 页边界内，异步写入
 * 完成后才增加 program_offset，确保失败时偏移仍反映最后确认完成的位置。
 */
static void ProcessProgram(update_service_t *service, int app)
{
    const package_file_id_t file = app != 0 ? PACKAGE_FILE_APP : PACKAGE_FILE_GUI;
    const manifest_app_component_t *component =
        app != 0 ? &service->manifest.app : &service->manifest.gui;
    const boot_error_t error = app != 0 ? BOOT_ERROR_APP_PROGRAM : BOOT_ERROR_GUI_PROGRAM;
    const uint32_t base =
        app != 0 ? service->runtime_layout->app_offset : service->runtime_layout->gui_offset;
    const uint32_t runtime_size =
        app != 0 ? service->runtime_layout->app_max_size : service->runtime_layout->gui_max_size;
    const update_stage_t read_stage =
        app != 0 ? UPDATE_STAGE_APP_PROGRAM_READ : UPDATE_STAGE_GUI_PROGRAM_READ;
    const update_stage_t start_stage =
        app != 0 ? UPDATE_STAGE_APP_PROGRAM_START : UPDATE_STAGE_GUI_PROGRAM_START;
    const update_stage_t poll_stage =
        app != 0 ? UPDATE_STAGE_APP_PROGRAM_POLL : UPDATE_STAGE_GUI_PROGRAM_POLL;
    const update_stage_t hash_stage =
        app != 0 ? UPDATE_STAGE_APP_PROGRAM_HASH : UPDATE_STAGE_GUI_PROGRAM_HASH;
    const update_stage_t target_read_stage =
        app != 0 ? UPDATE_STAGE_APP_TARGET_READ : UPDATE_STAGE_GUI_TARGET_READ;
    async_block_device_operation_result_t operation;
    firmware_status_t status;
    uint32_t chunk;
    uint32_t bytes_read;

    switch (service->stage)
    {
        case UPDATE_STAGE_APP_PROGRAM_OPEN:
        case UPDATE_STAGE_GUI_PROGRAM_OPEN:
            /* 再次打开并检查源文件，防止预检结束后文件被换成不同长度的内容。 */
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
            /* 计算不跨页的下一块；零长度意味着全部 payload 已交给 Flash。 */
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
            /* 同一块先加入源哈希，再交给 program_start，保证写入内容可追溯。 */
            status = service->package_source->read_at(service->package_source->context,
                                                      service->program_offset, service->io_buffer,
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
            service->stage                = start_stage;
            break;
        case UPDATE_STAGE_APP_PROGRAM_START:
        case UPDATE_STAGE_GUI_PROGRAM_START:
            /* Adapter 将该请求转为异步页编程；本阶段不假设写入已完成。 */
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
            /* 只有底层报告成功，才提交该块的逻辑偏移。 */
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
                Fail(service,
                     FirmwareStatus_IsOk(operation.status) ? FIRMWARE_STATUS_IO_ERROR
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
            /* 写入期间重新计算的源摘要必须仍等于 Manifest，防止介质中途变化。 */
            status = HashFinish(service, service->source_digest);
            if (!FirmwareStatus_IsOk(status) || (memcmp(service->source_digest, component->sha256,
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
            /* 源文件已关闭后，切换到独立哈希上下文对目标 Runtime 做回读验证。 */
            service->target_offset = 0U;
            status                 = HashReset(service);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status,
                     app != 0 ? BOOT_ERROR_APP_TARGET_HASH : BOOT_ERROR_GUI_TARGET_HASH);
                break;
            }
            service->stage = target_read_stage;
            break;
        default:
            break;
    }
}

/**
 * @brief 分块回读 APP 或 GUI Runtime，并验证其最终 SHA-256。
 *
 * 此步骤验证的是实际写入 Flash 的字节，而非仍在 SD/eMMC 上的源文件；每个被选择
 * 的组件都通过回读校验后，才能创建候选 Active Record。
 */
static void ProcessTarget(update_service_t *service, int app)
{
    uint32_t size = app != 0 ? service->manifest.app.size_bytes : service->manifest.gui.size_bytes;
    uint32_t base =
        app != 0 ? service->runtime_layout->app_offset : service->runtime_layout->gui_offset;
    update_stage_t read_stage =
        app != 0 ? UPDATE_STAGE_APP_TARGET_READ : UPDATE_STAGE_GUI_TARGET_READ;
    update_stage_t hash_stage =
        app != 0 ? UPDATE_STAGE_APP_TARGET_HASH : UPDATE_STAGE_GUI_TARGET_HASH;
    boot_error_t read_error = app != 0 ? BOOT_ERROR_APP_TARGET_READ : BOOT_ERROR_GUI_TARGET_READ;
    boot_error_t hash_error = app != 0 ? BOOT_ERROR_APP_TARGET_HASH : BOOT_ERROR_GUI_TARGET_HASH;
    firmware_status_t status;
    uint32_t chunk;

    if (service->stage == read_stage)
    {
        /* 逐块从固定 Flash 偏移读取，不通过 XIP 地址读取以避免 Cache 歧义。 */
        if (service->target_offset >= size)
        {
            service->stage = hash_stage;
            return;
        }
        /* 最后一块可短于 io_buffer，但绝不超过 Manifest 声明的真实 payload 长度。 */
        chunk  = MinU32(service->io_buffer_size, size - service->target_offset);
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

    /* 所有目标字节都被计入后，完成摘要并与 Manifest 中的安装目标比较。 */
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
    /* APP 验证后仅在 GUI 被选择时继续，否则直接生成候选记录。 */
    service->stage = (app != 0) &&
                             ((service->request.component_mask & UPDATE_COMPONENT_GUI) != 0U)
                         ? UPDATE_STAGE_GUI_ERASE
                         : UPDATE_STAGE_BUILD_RECORD_CANDIDATE;
    service->erase_offset = 0U;
}

void UpdateService_Process(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *) service;
    int source_gui;

    /* 非运行态调用是安全的空操作，防止 Application 轮询终态时触发重复 I/O。 */
    if ((implementation == NULL) || (implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }
    ReportInstallProgress(implementation);
    /* 保留低频阶段入口诊断；逐页编程和逐扇区擦除由限流总进度替代。 */
    if ((implementation->stage != implementation->logged_stage) ||
        (implementation->stage_logged == 0))
    {
        if (IsHighFrequencyStage(implementation->stage) == 0)
        {
            LOG_INFO("update", "stage=%s(%u) erase=0x%08lx program=0x%08lx target=0x%08lx",
                     UpdateStageName(implementation->stage), (unsigned) implementation->stage,
                     (unsigned long) implementation->erase_offset,
                     (unsigned long) implementation->program_offset,
                     (unsigned long) implementation->target_offset);
        }
        implementation->logged_stage = implementation->stage;
        implementation->stage_logged = 1;
    }
    if ((implementation->stage == UPDATE_STAGE_XIP_CHECK_INDIRECT) ||
        (implementation->stage == UPDATE_STAGE_XIP_EXIT) ||
        (implementation->stage == UPDATE_STAGE_XIP_VERIFY_INDIRECT))
    {
        ProcessEnsureIndirectMode(implementation);
        return;
    }
    if (implementation->stage == UPDATE_STAGE_CANCEL_CLOSE)
    {
        ProcessCancelClose(implementation);
        return;
    }
    if ((implementation->stage == UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_XIP_EXIT) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT))
    {
        ProcessCancelEnsureIndirectMode(implementation);
        return;
    }
    /* 使用连续枚举区间将细粒度状态委托给对应子状态机。 */
    if ((implementation->stage >= UPDATE_STAGE_PREPARE_MANIFEST_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_PREPARE_MANIFEST_PARSE))
    {
        ProcessPrepare(implementation);
        return;
    }
    /* APP 与 GUI 源预检复用同一处理函数，只通过参数决定当前组件。 */
    if ((implementation->stage >= UPDATE_STAGE_SOURCE_APP_OPEN) &&
        (implementation->stage <= UPDATE_STAGE_SOURCE_GUI_VERIFY))
    {
        source_gui = (implementation->stage >= UPDATE_STAGE_SOURCE_GUI_OPEN) ? 1 : 0;
        ProcessSourceHash(
            implementation, source_gui != 0 ? PACKAGE_FILE_GUI : PACKAGE_FILE_APP,
            source_gui != 0 ? &implementation->manifest.gui : &implementation->manifest.app,
            source_gui != 0 ? implementation->runtime_layout->gui_max_size
                            : implementation->runtime_layout->app_max_size,
            source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_SIZE : UPDATE_STAGE_SOURCE_APP_SIZE,
            source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_HASH : UPDATE_STAGE_SOURCE_APP_HASH,
            source_gui != 0 ? UPDATE_STAGE_SOURCE_GUI_VERIFY : UPDATE_STAGE_SOURCE_APP_VERIFY,
            source_gui != 0 ? BOOT_ERROR_GUI_SOURCE_SIZE : BOOT_ERROR_APP_SOURCE_SIZE,
            source_gui != 0 ? BOOT_ERROR_GUI_SOURCE_HASH : BOOT_ERROR_APP_SOURCE_HASH);
        return;
    }
    /* 以下顺序固定：APP 擦除/写入/回读，再 GUI 擦除/写入/回读。 */
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
        uint32_t selected = implementation->request.component_mask;

        /* 以当前记录为基底，只覆盖本次确实安装并回读校验通过的组件。 */
        if (implementation->base_record_valid != 0)
        {
            implementation->candidate_record = implementation->base_record;
        }
        else
        {
            memset(&implementation->candidate_record, 0,
                   sizeof(implementation->candidate_record));
        }
        implementation->candidate_record.component_mask |=
            selected & (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI);
        if ((implementation->candidate_record.component_mask &
             (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI)) == 0U)
        {
            implementation->candidate_record.component_mask =
                UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
        }
        implementation->candidate_record.format_version =
            ((implementation->candidate_record.component_mask & UPDATE_COMPONENT_THERAPY) != 0U)
                ? BOOT_ACTIVE_RECORD_FORMAT_V3
                : BOOT_ACTIVE_RECORD_FORMAT_V2;
        implementation->candidate_record.state           = BOOT_ACTIVE_RECORD_STATE_VALID;
        implementation->candidate_record.release_version = implementation->manifest.release_version;
        implementation->candidate_record.build_number    = implementation->manifest.build_number;
        memcpy(implementation->candidate_record.package_id_hash,
               implementation->manifest.package_id_hash128,
               sizeof(implementation->candidate_record.package_id_hash));
        memcpy(implementation->candidate_record.manifest_sha256,
               implementation->manifest.manifest_sha256,
               sizeof(implementation->candidate_record.manifest_sha256));
        if ((selected & UPDATE_COMPONENT_APP) != 0U)
        {
            implementation->candidate_record.app_size = implementation->manifest.app.size_bytes;
            memcpy(implementation->candidate_record.app_sha256,
                   implementation->manifest.app.sha256,
                   sizeof(implementation->candidate_record.app_sha256));
        }
        if ((selected & UPDATE_COMPONENT_GUI) != 0U)
        {
            implementation->candidate_record.gui_size = implementation->manifest.gui.size_bytes;
            memcpy(implementation->candidate_record.gui_sha256,
                   implementation->manifest.gui.sha256,
                   sizeof(implementation->candidate_record.gui_sha256));
        }
        /* Install 成功并不代表激活完成，等待 BootControlService 的后续提交。 */
        implementation->candidate_ready = 1;
        implementation->state           = SERVICE_RUN_STATE_SUCCEEDED;
        return;
    }

    if (implementation->stage == UPDATE_STAGE_FAILURE_CLOSE)
    {
        ProcessFailureClose(implementation);
        return;
    }

    /* RUNNING 却落入未知阶段时不能静默卡死，必须以内部错误结束。 */
    Fail(implementation, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INVALID_STATE);
}

firmware_status_t UpdateService_Cancel(struct update_service *service)
{
    update_service_t *implementation = (update_service_t *) service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /*
     * 以粘滞的真实写入标志而非枚举序号判断是否可取消。失败关闭阶段已经有根因和
     * 专属 close 恢复流程，不能用取消掩盖它；主动取消只适用于仍在正常前置流程中
     * 的请求。
     */
    if ((implementation->initialized == 0) ||
        (implementation->state != SERVICE_RUN_STATE_RUNNING) ||
        (implementation->runtime_may_be_modified != 0) ||
        (implementation->stage == UPDATE_STAGE_FAILURE_CLOSE) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_CLOSE) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_XIP_EXIT) ||
        (implementation->stage == UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /*
     * 初次 XIP 检查后，服务已经拥有“所有 Runtime 擦写必须 indirect”的责任。即使
     * 此刻还在源预检，也要在取消终态前重新查询并在需要时退出 memory-mapped。
     */
    implementation->cancel_requires_indirect =
        ((implementation->stage >= UPDATE_STAGE_XIP_CHECK_INDIRECT) &&
         (implementation->stage <= UPDATE_STAGE_SOURCE_GUI_VERIFY))
            ? 1
            : 0;
    implementation->close_retry_count = 0U;
    implementation->candidate_ready   = 0;
    if (implementation->source_file_open != 0)
    {
        implementation->stage = UPDATE_STAGE_CANCEL_CLOSE;
        return FIRMWARE_STATUS_OK;
    }
    ContinueCancellationCleanup(implementation);
    return FIRMWARE_STATUS_OK;
}

service_run_state_t UpdateService_GetState(const struct update_service *service)
{
    /* 读取 NULL 服务时返回 FAILED，使 Application 保持 fail-closed。 */
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *UpdateService_GetResult(const struct update_service *service)
{
    /* 结果在下一次 PrepareStart 前由服务对象持有。 */
    return (service == NULL) ? NULL : &service->result;
}

const validated_manifest_t *UpdateService_GetManifest(const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *) service;

    /* 只暴露已完成完整绑定校验的 Manifest，清理阶段不能借枚举顺序泄漏半成品。 */
    return (implementation == NULL) || (implementation->manifest_ready == 0)
               ? NULL
               : &implementation->manifest;
}

const boot_active_record_t *UpdateService_GetCandidate(const struct update_service *service)
{
    const update_service_t *implementation = (const update_service_t *) service;

    /* 只有所有选中 Runtime 目标回读均成功后，候选记录才可交给 EEPROM 提交服务。 */
    return (implementation == NULL) || (implementation->candidate_ready == 0)
               ? NULL
               : &implementation->candidate_record;
}

int UpdateService_RuntimeMayBeModified(const struct update_service *service)
{
    /* 该标志一旦在 APP 首次擦除时置位，直到下次 Prepare 都保持为真。 */
    return (service != NULL) &&
           (((const update_service_t *) service)->runtime_may_be_modified != 0);
}
