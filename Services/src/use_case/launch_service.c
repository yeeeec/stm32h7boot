/**
 * @file launch_service.c
 * @brief XIP 启动编排实现。
 *
 * 本模块以同步、不可回滚的方式完成最后交接：先在 QSPI indirect 模式读取并
 * 验证向量表，再进入 memory-mapped 读取模式、失效 Cache，最后跳转到 APP。
 */
#include "services/use_case/launch_service.h"

#include <stddef.h>

#include "logging.h"
#include "services/common/runtime_layout.h"

typedef enum
{
    /** 尚未执行启动操作。 */
    LAUNCH_STAGE_IDLE = 0,
    /** 读取 APP 向量表的前两个 word。 */
    LAUNCH_STAGE_READ_VECTOR,
    /** 检查 MSP 和 Reset Handler 的地址规则。 */
    LAUNCH_STAGE_VALIDATE_VECTOR,
    /** 请求 QSPI 进入 memory-mapped 读取模式。 */
    LAUNCH_STAGE_ENTER_XIP,
    /** 使 APP 映射范围相关的 Cache 内容失效。 */
    LAUNCH_STAGE_INVALIDATE_CACHE,
    /** 执行 Cortex-M VTOR/MSP/PC 交接。 */
    LAUNCH_STAGE_JUMP
} launch_stage_t;

/** 将启动阶段转换为日志文本，便于定位一次性同步流程的失败位置。 */
static const char *LaunchStageName(launch_stage_t stage)
{
    switch (stage)
    {
        case LAUNCH_STAGE_IDLE:
            return "idle";
        case LAUNCH_STAGE_READ_VECTOR:
            return "read-vector";
        case LAUNCH_STAGE_VALIDATE_VECTOR:
            return "validate-vector";
        case LAUNCH_STAGE_ENTER_XIP:
            return "enter-xip";
        case LAUNCH_STAGE_INVALIDATE_CACHE:
            return "invalidate-cache";
        case LAUNCH_STAGE_JUMP:
            return "jump";
        default:
            return "unknown";
    }
}

/** 以 Cortex-M 小端格式解码向量表中的 32 位值。 */
static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

/**
 * @brief 记录启动失败并填充结果快照。
 *
 * Launch 仅在失败时返回，因此所有返回路径都通过本函数保留阶段和业务错误。
 */
static firmware_status_t Fail(launch_service_t *service, firmware_status_t status,
                              boot_error_t error, launch_stage_t stage)
{
    LOG_ERROR("launch", "failed: status=%d error=%d stage=%s", (int) status, (int) error,
              LaunchStageName(stage));
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) stage;
    service->result.native_error = (int32_t) status;
    return status;
}

firmware_status_t LaunchService_Init(launch_service_t *service,
                                     const launch_service_dependencies_t *dependencies)
{
    /* XIP、跳转和向量校验缺一不可；初始化时提前拒绝不完整组合。 */
    if ((service == NULL) || (dependencies == NULL) || (dependencies->storage == NULL) ||
        (dependencies->storage->read == NULL) || (dependencies->xip_controller == NULL) ||
        (dependencies->xip_controller->enter_memory_mapped_read == NULL) ||
        (dependencies->xip_controller->is_memory_mapped == NULL) ||
        (dependencies->xip_controller->invalidate_mapped_cache == NULL) ||
        (dependencies->application_jump == NULL) ||
        (dependencies->application_jump->execute == NULL) || (dependencies->sram_regions == NULL) ||
        (dependencies->sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 保存 Composition 注入的借用指针；Launch Service 自身不拥有硬件对象。 */
    service->storage             = dependencies->storage;
    service->xip_controller      = dependencies->xip_controller;
    service->application_jump    = dependencies->application_jump;
    service->sram_regions        = dependencies->sram_regions;
    service->sram_region_count   = dependencies->sram_region_count;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = LAUNCH_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t LaunchService_Execute(launch_service_t *service,
                                        const boot_active_record_t *active_record)
{
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();
    boot_region_t app_region;
    vector_table_values_t vectors;
    uint8_t vector_bytes[8];
    firmware_status_t status;
    int mapped;

    /* Active Record 是唯一被授权的 Runtime 描述，缺失时禁止直接跳转。 */
    if ((service == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((active_record->app_size == 0U) || (active_record->app_size > layout->app_max_size))
    {
        return Fail(service, FIRMWARE_STATUS_OUT_OF_RANGE, BOOT_ERROR_CONTROL_RECORD,
                    LAUNCH_STAGE_READ_VECTOR);
    }
    /* 同一分区描述同时约束 indirect 读取与 Reset Handler 的 XIP 地址范围。 */
    app_region = (boot_region_t) {
        layout->app_offset,
        layout->app_xip_base,
        layout->app_max_size,
    };
    LOG_INFO("launch", "start: app_size=%lu mapped=0x%08lx",
             (unsigned long) active_record->app_size, (unsigned long) BOOT_APP_RUNTIME_BASE);
    /* 读取向量表前必须保持 indirect 模式，避免复用未知的上一轮 XIP 状态。 */
    status = service->xip_controller->is_memory_mapped(service->xip_controller->context, &mapped);
    if (!FirmwareStatus_IsOk(status) || (mapped != 0))
    {
        return Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                    BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_READ_VECTOR);
    }
    /* 从固定 APP 偏移读取 MSP 和 Reset_Handler；无需先建立 XIP。 */
    status = service->storage->read(service->storage->context, BOOT_APP_FLASH_OFFSET,
                                    vector_bytes, sizeof(vector_bytes));
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_MEDIA_UNAVAILABLE, LAUNCH_STAGE_READ_VECTOR);
    }

    /* 向量表前两个 word 的含义由 Cortex-M 架构固定。 */
    vectors.initial_msp   = ReadU32(&vector_bytes[0]);
    vectors.reset_handler = ReadU32(&vector_bytes[4]);
    LOG_INFO("launch", "vector: msp=0x%08lx reset=0x%08lx", (unsigned long) vectors.initial_msp,
             (unsigned long) vectors.reset_handler);
    status = VectorValidation_Validate(&vectors, &app_region, active_record->app_size,
                                       service->sram_regions, service->sram_region_count);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_VECTOR_TABLE, LAUNCH_STAGE_VALIDATE_VECTOR);
    }

    /* 验证通过后才暴露 XIP 窗口，防止错误镜像进入可执行状态。 */
    status = service->xip_controller->enter_memory_mapped_read(service->xip_controller->context);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_ENTER_XIP);
    }
    LOG_INFO("launch", "xip memory-mapped read enabled");
    /* 安装或之前的 indirect 访问可能留下旧 Cache 行，交接前必须失效。 */
    status = service->xip_controller->invalidate_mapped_cache(
        service->xip_controller->context, BOOT_APP_RUNTIME_BASE, active_record->app_size);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_INVALIDATE_CACHE);
    }

    LOG_WARN("launch", "jumping to application: address=0x%08lx",
             (unsigned long) BOOT_APP_RUNTIME_BASE);
    /* 成功跳转不返回；若返回则平台交接失败并按 fail-closed 处理。 */
    status = service->application_jump->execute(service->application_jump->context,
                                                BOOT_APP_RUNTIME_BASE);
    return Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                BOOT_ERROR_INTERNAL, LAUNCH_STAGE_JUMP);
}

const service_result_t *LaunchService_GetResult(const launch_service_t *service)
{
    /* 返回由 Service 长期保存的只读结果快照。 */
    return (service == NULL) ? NULL : &service->result;
}
