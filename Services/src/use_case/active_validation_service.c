/**
 * @file active_validation_service.c
 * @brief 对固定 APP/GUI Runtime 执行增量 SHA-256 与向量表校验。
 *
 * 本服务始终通过 indirect Flash 接口读取数据，因此不会依赖 Application 已经
 * 建立的 XIP 映射。它把完整镜像扫描拆分为有限大小的 Process 步骤，避免长时间
 * 阻塞主循环，并在 APP 首块额外验证 Cortex-M 启动向量。
 */
#include "services/use_case/active_validation_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"

/** 将内部阶段转换为稳定的日志文字；仅用于诊断，不参与状态决策。 */
static const char *ActiveValidationStageName(active_validation_stage_t stage)
{
    switch (stage)
    {
        case ACTIVE_VALIDATION_STAGE_IDLE:
            return "idle";
        case ACTIVE_VALIDATION_STAGE_RESET_APP_HASH:
            return "reset-app-hash";
        case ACTIVE_VALIDATION_STAGE_READ_APP:
            return "read-app";
        case ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH:
            return "finish-app-hash";
        case ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH:
            return "reset-gui-hash";
        case ACTIVE_VALIDATION_STAGE_READ_GUI:
            return "read-gui";
        case ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH:
            return "finish-gui-hash";
        default:
            return "unknown";
    }
}

/**
 * @brief 以小端序从向量表字节读取一个 32 位值。
 *
 * Cortex-M 向量表采用小端序。本函数只在已确认至少读取 8 字节的上下文中使用。
 */
static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

/**
 * @brief 以统一格式记录失败，并冻结对外可见的结果快照。
 *
 * 失败后不再推进任何读取或哈希步骤；Application 根据 state 和 result 决定继续
 * 当前 Runtime、进入恢复路径或 fail-closed。
 */
static void Fail(active_validation_service_t *service, firmware_status_t status, boot_error_t error)
{
    LOG_ERROR("active", "failed: status=%d error=%d stage=%s", (int) status, (int) error,
              ActiveValidationStageName(service->stage));
    service->state               = SERVICE_RUN_STATE_FAILED;
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = (int32_t) status;
}

firmware_status_t
ActiveValidationService_Init(active_validation_service_t *service,
                             const active_validation_service_dependencies_t *dependencies)
{
    /* 初始化阶段一次性检查所有回调，运行时路径无需反复防御空函数指针。 */
    if ((service == NULL) || (dependencies == NULL) || (dependencies->storage == NULL) ||
        (dependencies->storage->read == NULL) || (dependencies->hash == NULL) ||
        (dependencies->hash->reset == NULL) || (dependencies->hash->update == NULL) ||
        (dependencies->hash->finish == NULL) || (dependencies->buffer == NULL) ||
        (dependencies->buffer_size < 8U) || (dependencies->sram_regions == NULL) ||
        (dependencies->sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* Service 只保存借用指针；依赖与缓冲区由 Composition 持有。 */
    service->storage             = dependencies->storage;
    service->hash                = dependencies->hash;
    service->buffer              = dependencies->buffer;
    service->buffer_size         = dependencies->buffer_size;
    service->sram_regions        = dependencies->sram_regions;
    service->sram_region_count   = dependencies->sram_region_count;
    service->state               = SERVICE_RUN_STATE_IDLE;
    service->stage               = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ActiveValidationService_Start(active_validation_service_t *service,
                                                const boot_active_record_t *active_record)
{
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();

    /* Start 可以复用已完成的实例，但绝不允许覆盖仍在运行中的上下文。 */
    if ((service == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 先拒绝不可能落入固定分区的记录，再开始任何外部 Flash 访问。 */
    if ((active_record->app_size == 0U) || (active_record->app_size > layout->app_max_size) ||
        (active_record->gui_size == 0U) || (active_record->gui_size > layout->gui_max_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    /* 复制记录，保证长流程不受调用者随后修改原对象的影响。 */
    service->active_record       = *active_record;
    service->layout              = layout;
    service->offset              = 0U;
    service->stage               = ACTIVE_VALIDATION_STAGE_RESET_APP_HASH;
    service->state               = SERVICE_RUN_STATE_RUNNING;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    LOG_INFO("active", "started: app=%lu gui=%lu", (unsigned long) active_record->app_size,
             (unsigned long) active_record->gui_size);
    return FIRMWARE_STATUS_OK;
}

/**
 * @brief 读取并哈希当前组件的一个有界数据块。
 *
 * APP 的第一块携带向量表，因此 validate_vectors 为真时会在哈希前检查 MSP 和
 * Reset Handler。GUI 仅做完整性校验，不承载 Bootloader 跳转向量。
 */
static void ReadComponent(active_validation_service_t *service, const boot_region_t *region,
                          uint32_t image_size, int validate_vectors, boot_error_t read_error)
{
    /* offset 始终由状态机限制在 image_size 内，单步最多读取 buffer_size。 */
    uint32_t remaining = image_size - service->offset;
    uint32_t read_size = (remaining < service->buffer_size) ? remaining : service->buffer_size;
    firmware_status_t status =
        service->storage->read(service->storage->context, region->flash_offset + service->offset,
                               service->buffer, read_size);

    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, read_error);
        return;
    }
    if ((validate_vectors != 0) && (service->offset == 0U))
    {
        vector_table_values_t vectors;

        /* APP 的前 8 字节分别为初始 MSP 与 Reset_Handler。 */
        vectors.initial_msp   = ReadU32(&service->buffer[0]);
        vectors.reset_handler = ReadU32(&service->buffer[4]);
        LOG_DEBUG("active", "vector read: msp=0x%08lx reset=0x%08lx",
                  (unsigned long) vectors.initial_msp, (unsigned long) vectors.reset_handler);
        status = VectorValidation_Validate(&vectors, region, image_size, service->sram_regions,
                                           service->sram_region_count);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, BOOT_ERROR_VECTOR_TABLE);
            return;
        }
    }
    /* 只有读取与向量验证均成功后，才将该块计入组件摘要。 */
    status = service->hash->update(service->hash->context, service->buffer, read_size);
    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, BOOT_ERROR_INTERNAL);
        return;
    }

    service->offset += read_size;
}

void ActiveValidationService_Process(active_validation_service_t *service)
{
    firmware_status_t status;
    boot_region_t app_region;
    boot_region_t gui_region;
    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE];

    /* Process 是幂等空操作：未运行或无对象时不产生新的底层 I/O。 */
    if ((service == NULL) || (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case ACTIVE_VALIDATION_STAGE_RESET_APP_HASH:
            /* 每个组件单独计算摘要，禁止 APP 与 GUI 串接到同一哈希上下文。 */
            status = service->hash->reset(service->hash->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage  = ACTIVE_VALIDATION_STAGE_READ_APP;
            LOG_INFO("active", "app hash scan started: size=%lu",
                     (unsigned long) service->active_record.app_size);
            break;

        case ACTIVE_VALIDATION_STAGE_READ_APP:
            /* 构造 APP 的 indirect 偏移与 XIP 地址边界，供向量校验共用。 */
            app_region = (boot_region_t) {
                service->layout->app_offset,
                service->layout->app_xip_base,
                service->layout->app_max_size,
            };
            ReadComponent(service, &app_region, service->active_record.app_size, 1,
                          BOOT_ERROR_APP_TARGET_HASH);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.app_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH:
            /* 完成值必须与 Active Record 中的安装时摘要逐字节一致。 */
            status = service->hash->finish(service->hash->context, digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(digest, service->active_record.app_sha256, sizeof(digest)) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                     BOOT_ERROR_APP_TARGET_HASH);
                break;
            }
            LOG_INFO("active", "app hash verified");
            service->state               = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status       = FIRMWARE_STATUS_OK;
            service->result.error        = BOOT_ERROR_NONE;
            service->result.stage        = (uint32_t) service->stage;
            service->result.native_error = 0;
            service->stage               = ACTIVE_VALIDATION_STAGE_IDLE;
            // service->stage = ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH;
            break;

        case ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH:
            /* APP 成功后才开始 GUI；任一组件失败都会阻止启动。 */
            status = service->hash->reset(service->hash->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage  = ACTIVE_VALIDATION_STAGE_READ_GUI;
            LOG_INFO("active", "gui hash scan started: size=%lu",
                     (unsigned long) service->active_record.gui_size);
            break;

        case ACTIVE_VALIDATION_STAGE_READ_GUI:
            /* GUI 不执行跳转，因此不进行 MSP/Reset Handler 校验。 */
            gui_region = (boot_region_t) {
                service->layout->gui_offset,
                service->layout->gui_mmap_base,
                service->layout->gui_max_size,
            };
            ReadComponent(service, &gui_region, service->active_record.gui_size, 0,
                          BOOT_ERROR_GUI_TARGET_HASH);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.gui_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH:
            /* 两个组件均通过后才向 Application 报告成功。 */
            status = service->hash->finish(service->hash->context, digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(digest, service->active_record.gui_sha256, sizeof(digest)) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                     BOOT_ERROR_GUI_TARGET_HASH);
                break;
            }
            LOG_INFO("active", "gui hash verified");
            service->state               = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status       = FIRMWARE_STATUS_OK;
            service->result.error        = BOOT_ERROR_NONE;
            service->result.stage        = (uint32_t) service->stage;
            service->result.native_error = 0;
            service->stage               = ACTIVE_VALIDATION_STAGE_IDLE;
            break;

        default:
            /* 未知阶段表示内存或调用顺序破坏，按内部错误 fail-closed。 */
            Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INTERNAL);
            break;
    }
}

service_run_state_t ActiveValidationService_GetState(const active_validation_service_t *service)
{
    /* 对 NULL 返回 FAILED，调用者无需对读取状态额外判空。 */
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *
ActiveValidationService_GetResult(const active_validation_service_t *service)
{
    /* 结果由服务对象持有，在下次 Start 前保持有效。 */
    return (service == NULL) ? NULL : &service->result;
}
