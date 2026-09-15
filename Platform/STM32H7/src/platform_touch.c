/**
 * @file platform_touch.c
 * @brief 将板级触摸驱动适配为不暴露具体控制器名称的输入端口。
 */

#include "platform/platform_ports.h"

#include <stddef.h>
#include <string.h>

#include "bsp/bsp_touch.h"

/** 读取 BSP 触摸状态并转换为通用输入结构。 */
static firmware_status_t TouchRead(void *context, touch_input_state_t *state)
{
    bsp_touch_state_t raw;
    firmware_status_t status;

    (void) context;
    if (state == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    (void) memset(&raw, 0, sizeof(raw));
    (void) memset(state, 0, sizeof(*state));

    status = BSP_TouchGetState(&raw);
    if (FirmwareStatus_IsError(status))
        return status;

    state->has_update = raw.hasUpdate;
    state->detected   = raw.detected;
    state->x          = raw.x;
    state->y          = raw.y;
    return FIRMWARE_STATUS_OK;
}

/** 返回触摸 BSP 是否已经初始化。 */
static bool TouchIsReady(void *context)
{
    (void) context;
    return BSP_TouchIsInitialized() != 0;
}

/** 导出供 TouchTask 使用的触摸输入端口。 */
firmware_status_t Platform_GetTouchPort(touch_input_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    *port = (touch_input_port_t) {
        .context  = NULL,
        .read     = TouchRead,
        .is_ready = TouchIsReady,
    };
    return FIRMWARE_STATUS_OK;
}
