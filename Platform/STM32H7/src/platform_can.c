/**
 * @file platform_can.c
 * @brief 将 BSP CAN 驱动适配为 Platform/Services 使用的统一传输端口。
 */

#include "platform/platform_can.h"
#include "platform/platform_ports.h"

#include <stddef.h>

#include "bsp/bsp_can.h"

/** can_transport_port_t 的错误恢复回调。 */
static firmware_status_t TransportRecover(void *context)
{
    (void) context;
    return Platform_CanRecover();
}

/** can_transport_port_t 的统计读取回调。 */
static void TransportGetStatistics(void *context, can_transport_statistics_t *statistics)
{
    (void) context;
    if (statistics == NULL)
        return;
    Platform_CanGetStatistics(statistics);
}

/** can_transport_port_t 的发送回调。 */
static firmware_status_t TransportSend(void *context, const can_transport_frame_t *frame)
{
    (void) context;
    return Platform_CanSend(frame);
}

/** can_transport_port_t 的接收回调。 */
static bool TransportRead(void *context, can_transport_frame_t *frame)
{
    (void) context;
    return Platform_CanRead(frame);
}

/** can_transport_port_t 的状态查询回调。 */
static can_transport_state_t TransportGetState(void *context)
{
    (void) context;
    return Platform_CanGetState();
}

/** 启动底层 CAN 控制器。 */
firmware_status_t Platform_CanStart(void)
{
    return BSP_CAN_Start();
}

/** 停止底层 CAN 控制器。 */
firmware_status_t Platform_CanStop(void)
{
    return BSP_CAN_Stop();
}

/** 请求底层 CAN 从错误状态恢复。 */
firmware_status_t Platform_CanRecover(void)
{
    return BSP_CAN_Recover();
}

/** 校验标准帧范围后发送 CAN 帧。 */
firmware_status_t Platform_CanSend(const can_transport_frame_t *frame)
{
    if (frame == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (frame->id > 0x7FFU || frame->dlc > CAN_TRANSPORT_MAX_DATA_BYTES)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    return BSP_CAN_Send(frame);
}

/** 尝试从 BSP 接收队列读取一帧 CAN 数据。 */
bool Platform_CanRead(can_transport_frame_t *frame)
{
    return frame != NULL && BSP_CAN_Read(frame);
}

/** 获取底层 CAN 当前状态。 */
can_transport_state_t Platform_CanGetState(void)
{
    return BSP_CAN_GetState();
}

/** 获取底层 CAN 累计统计。 */
void Platform_CanGetStatistics(can_transport_statistics_t *statistics)
{
    if (statistics == NULL)
        return;
    BSP_CAN_GetStatistics(statistics);
}

/** 导出供 Communication 服务使用的 CAN 能力端口。 */
firmware_status_t Platform_GetCanTransportPort(can_transport_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (can_transport_port_t) {
        .context        = NULL,
        .recover        = TransportRecover,
        .send           = TransportSend,
        .read           = TransportRead,
        .get_state      = TransportGetState,
        .get_statistics = TransportGetStatistics,
    };
    return FIRMWARE_STATUS_OK;
}
