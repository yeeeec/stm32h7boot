#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "firmware/status.h"
#include <stdint.h>

firmware_status_t BspI2c_Read(uint8_t address_7bit, uint16_t memory_address, uint8_t *data,
                              uint32_t size);

firmware_status_t BspI2c_Write(uint8_t address_7bit, uint16_t memory_address, const uint8_t *data,
                               uint32_t size);

firmware_status_t BspI2c_IsReady(uint8_t address_7bit);

#endif