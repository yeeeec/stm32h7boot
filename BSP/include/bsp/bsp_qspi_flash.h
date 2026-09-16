#ifndef BSP_QSPI_FLASH_H
#define BSP_QSPI_FLASH_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t instruction;
        uint8_t address_bytes;
        uint8_t dummy_cycles;
        uint32_t address;
    } bsp_qspi_flash_transaction_t;

    /*
     * CubeMX must initialize hqspi before these functions are called.
     * address_bytes supports 0, 3, or 4 bytes.
     */
    firmware_status_t BspQspiFlash_Command(const bsp_qspi_flash_transaction_t *transaction);

    firmware_status_t BspQspiFlash_Receive(const bsp_qspi_flash_transaction_t *transaction,
                                           uint8_t *data, uint32_t size);

    firmware_status_t BspQspiFlash_Transmit(const bsp_qspi_flash_transaction_t *transaction,
                                            const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* BSP_QSPI_FLASH_H */
