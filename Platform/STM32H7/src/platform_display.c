/**
 * @file platform.c
 * @brief Platform 层基础能力：初始化 CAN、提供单调时钟和看门狗端口。
 */

#include "platform/platform.h"
#include "platform/platform_ports.h"

#include <stddef.h>

#include "stm32h7xx_hal.h"

#include "bsp/bsp_display.h"

static firmware_status_t Init(void *context)
{
    (void) context;
    return BSP_BacklightInit();
}

static firmware_status_t SetPrecent(void *context, uint16_t percent)
{
    (void) context;
    BSP_BacklightSetPermille(percent);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SetOnOff(void *context, bool onoff)
{
    (void) context;
    (onoff) ? BSP_BacklightEnable() : BSP_BacklightDisable();
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Platform_GetBackLightPort(backlight_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (backlight_port_t) {
        .context = NULL, .init = Init, .precent = SetPrecent, .onoff = SetOnOff};
    return FIRMWARE_STATUS_OK;
}