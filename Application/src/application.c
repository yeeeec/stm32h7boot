/**
 * @file application.c
 * @brief Application 层组合根：绑定 Platform 能力并创建各 RTOS 任务。
 *
 * 本文件只负责依赖注入和任务启动顺序，不实现具体业务；SD、USB 及参数
 * 存储的实际生命周期分别由对应任务拥有。
 */

#include "application/application.h"

#include "platform/platform_ports.h"

/* Application_Init 的一次性启动标志，只在本文件中读写。 */
static int s_initialized;

/** 将日志类型解析为 Platform 提供的具体输出端口。 */
static firmware_status_t ResolveLogPort(void *context, log_output_kind_t kind,
                                        log_output_port_t *port)
{
    (void) context;
    return Platform_GetLogPort(kind, port);
}

/** 初始化消息总线、能力端口并创建 Application 层所有任务。 */
firmware_status_t Application_Init(void)
{
    clock_port_t clock          = {0};
    runtime_port_t runtime      = {0};
    log_output_port_t logOutput = {0};
    backlight_port_t backLight  = {0};


    if (s_initialized != 0)
        return FIRMWARE_STATUS_OK;

    /* 先绑定不可替换的运行时能力，再初始化使用者。 */
    if (Platform_GetBackLightPort(&backLight) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (backLight.init == NULL || backLight.init(backLight.context) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (Platform_GetClockPort(&clock) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (Platform_GetRuntimePort(&runtime) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;

    if (SystemTask_SetBackLightPort(&backLight) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (TaskHealth_SetClockPort(&clock) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (Application_UiSetClockPort(&clock) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (SystemTask_SetRuntimePort(&runtime) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (LogTask_SetClockPort(&clock) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (LogTask_SetOutputResolver(ResolveLogPort, NULL) != FIRMWARE_STATUS_OK)
        return FIRMWARE_STATUS_IO_ERROR;

    /* 创建任务前解析可替换端口，避免绑定失败留下半启动应用。 */
    // if (Platform_GetCanTransportPort(&communicationConfig.can) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // communicationConfig.clock = clock;
    // if (Platform_GetTouchPort(&touchInput) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (Platform_GetLogPort(LOG_OUTPUT_UART, &logOutput) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (Platform_GetParameterStoragePort(&parameterStorage) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (CommTask_SetCommunicationConfig(&communicationConfig) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (TouchTask_SetInputPort(&touchInput) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (LogTask_SetOutputPort(&logOutput) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (ParametersTask_SetStoragePort(&parameterStorage) != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;

    // if (SystemTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (AlarmTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (CommTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // /* UpdateTask 发起首个 SD 事务前必须确保 StorageTask 已运行；在包验证通过前，
    //  * UpdateTask 仍独占 USB 且不依赖 SD。 */
    // if (StorageTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (ParametersTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (UpdateTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (LogTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (TouchTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    // if (AppUiTask_Create() != FIRMWARE_STATUS_OK)
    //     return FIRMWARE_STATUS_IO_ERROR;
    s_initialized = 1;
    return FIRMWARE_STATUS_OK;
}