#ifndef SPI_NOR_BLOCK_ADAPTER_H
#define SPI_NOR_BLOCK_ADAPTER_H

#include "firmware/block_device.h"

struct spi_nor;

typedef struct
{
    block_device_t interface;
    struct spi_nor *device;
} spi_nor_block_adapter_t;

firmware_status_t SpiNorBlockAdapter_Init(
    spi_nor_block_adapter_t *adapter,
    struct spi_nor *device);
const block_device_t *SpiNorBlockAdapter_Interface(
    const spi_nor_block_adapter_t *adapter);

#endif
