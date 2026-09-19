#ifndef BSP_AT24_BUS_H
#define BSP_AT24_BUS_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t BspAt24Bus_Read(uint8_t address_7bit, uint16_t memory_address,
                                      uint8_t *data, uint32_t size);
    firmware_status_t BspAt24Bus_Write(uint8_t address_7bit, uint16_t memory_address,
                                       const uint8_t *data, uint32_t size);
    firmware_status_t BspAt24Bus_Probe(uint8_t address_7bit);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AT24_BUS_H */
