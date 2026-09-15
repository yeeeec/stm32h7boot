/**
 * @file bsp_touch.h
 * @brief GT911 board binding on the shared I2C1 bus.
 */
#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t device_address_7bit;
        uint32_t transfer_timeout_ms;
    } bsp_touch_config_t;

    typedef struct
    {
        uint8_t hasUpdate;
        uint8_t detected;
        uint16_t x;
        uint16_t y;
    } bsp_touch_state_t;

    firmware_status_t BSP_TouchInit(const bsp_touch_config_t *config);
    firmware_status_t BSP_TouchGetState(bsp_touch_state_t *state);
    int BSP_TouchIsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif
