/**
 * @file bsp_i2c_bus.h
 * @brief Shared, serialized access to the board I2C1 bus.
 */
#ifndef BSP_I2C_BUS_H
#define BSP_I2C_BUS_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        BSP_I2C_MEMORY_ADDRESS_8BIT = 1,
        BSP_I2C_MEMORY_ADDRESS_16BIT
    } bsp_i2c_memory_address_size_t;

    /** Create the RTOS mutex after osKernelInitialize() and before tasks start. */
    firmware_status_t BSP_I2cBusRtosInit(void);

    /** Return non-zero after CubeMX has initialized I2C1. */
    int BSP_I2c1IsReady(void);

    firmware_status_t BSP_I2c1MemRead(uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                      bsp_i2c_memory_address_size_t memoryAddressSize,
                                      uint8_t *data, uint16_t size, uint32_t timeoutMs);

    firmware_status_t BSP_I2c1MemWrite(uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                       bsp_i2c_memory_address_size_t memoryAddressSize,
                                       const uint8_t *data, uint16_t size, uint32_t timeoutMs);

    /**
     * Probe one 7-bit address. A normal address NACK is reported as ready == 0,
     * not as a transport failure; this is required for EEPROM acknowledge polling.
     */
    firmware_status_t BSP_I2c1Probe(uint8_t deviceAddress7bit, uint32_t timeoutMs, int *ready);

#ifdef __cplusplus
}
#endif

#endif
