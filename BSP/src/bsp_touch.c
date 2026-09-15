/**
 * @file bsp_touch.c
 * @brief GT911 binding to the board's shared I2C1 bus.
 */
#include "bsp/bsp_touch.h"

#include <stddef.h>
#include <string.h>

#include "bsp/bsp_i2c_bus.h"
#include "gt911.h"
#include "stm32h7xx_hal.h"

static GT911_Object_t s_touch;
static uint8_t s_address_7bit;
static uint32_t s_transfer_timeout_ms;
static int s_initialized;

static int32_t BusInit(void)
{
    return BSP_I2c1IsReady() ? GT911_OK : GT911_ERROR;
}

static int32_t BusDeInit(void)
{
    return GT911_OK;
}

static int32_t BusReadReg(uint16_t address, uint16_t reg, uint8_t *data, uint16_t size)
{
    firmware_status_t status = BSP_I2c1MemRead((uint8_t) address, reg, BSP_I2C_MEMORY_ADDRESS_16BIT,
                                               data, size, s_transfer_timeout_ms);
    return FirmwareStatus_IsOk(status) ? GT911_OK : GT911_ERROR;
}

static int32_t BusWriteReg(uint16_t address, uint16_t reg, uint8_t *data, uint16_t size)
{
    firmware_status_t status = BSP_I2c1MemWrite(
        (uint8_t) address, reg, BSP_I2C_MEMORY_ADDRESS_16BIT, data, size, s_transfer_timeout_ms);
    return FirmwareStatus_IsOk(status) ? GT911_OK : GT911_ERROR;
}

static int32_t GetTick(void)
{
    return (int32_t) HAL_GetTick();
}

firmware_status_t BSP_TouchInit(const bsp_touch_config_t *config)
{
    GT911_IO_t io = {0};
    uint32_t id   = 0U;
    if (config == NULL || config->device_address_7bit > 0x7FU || config->transfer_timeout_ms == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (s_initialized != 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;

    s_address_7bit        = config->device_address_7bit;
    s_transfer_timeout_ms = config->transfer_timeout_ms;
    io.Init               = BusInit;
    io.DeInit             = BusDeInit;
    io.Address            = s_address_7bit;
    io.WriteReg           = BusWriteReg;
    io.ReadReg            = BusReadReg;
    io.GetTick            = GetTick;
    (void) memset(&s_touch, 0, sizeof(s_touch));

    if (GT911_RegisterBusIO(&s_touch, &io) != GT911_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    if (GT911_ReadID(&s_touch, &id) != GT911_OK || id != GT911_ID)
    {
        (void) memset(&s_touch, 0, sizeof(s_touch));
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    if (GT911_Init(&s_touch) != GT911_OK)
    {
        (void) memset(&s_touch, 0, sizeof(s_touch));
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if (GT911_SetTriggerMode(&s_touch, GT911_M_SW1_INTERRUPT_RISING) != GT911_OK ||
        GT911_EnableIT(&s_touch) != GT911_OK)
    {
        (void) memset(&s_touch, 0, sizeof(s_touch));
        return FIRMWARE_STATUS_IO_ERROR;
    }
    s_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_TouchGetState(bsp_touch_state_t *state)
{
    GT911_State_t deviceState = {0};
    if (state == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(state, 0, sizeof(*state));
    if (s_initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (GT911_GetState(&s_touch, &deviceState) != GT911_OK)
        return FIRMWARE_STATUS_IO_ERROR;
    state->hasUpdate = deviceState.TouchReady != 0U ? 1U : 0U;
    state->detected  = deviceState.TouchDetected != 0U ? 1U : 0U;
    state->x         = (uint16_t) deviceState.TouchX;
    state->y         = (uint16_t) deviceState.TouchY;
    return FIRMWARE_STATUS_OK;
}

int BSP_TouchIsInitialized(void)
{
    return s_initialized;
}
