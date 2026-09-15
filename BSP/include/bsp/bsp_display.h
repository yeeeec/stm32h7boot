/**
 * @file bsp_display.h
 * @brief 外部屏幕black light的板级线性绑定。
 */
#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include "firmware/status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t BSP_BacklightInit(void);

    void BSP_BacklightSetPermille(uint16_t percent);

    void BSP_BacklightEnable(void);

    void BSP_BacklightDisable(void);

#ifdef __cplusplus
}
#endif


#endif