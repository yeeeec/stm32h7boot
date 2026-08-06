/**
 * @file spi_nor_block_adapter.h
 * @brief Adapter from the SPI NOR driver to the generic block-device interface.
 */
#ifndef SPI_NOR_BLOCK_ADAPTER_H
#define SPI_NOR_BLOCK_ADAPTER_H

#include "firmware/block_device.h"
#include "firmware/async_block_device.h"

struct spi_nor;

/** Owns the generic interface bound to a caller-owned SPI NOR device. */
typedef struct
{
    block_device_t interface;
    async_block_device_t async_interface;
    struct spi_nor *device;
} spi_nor_block_adapter_t;

/**
 * @brief Bind a block-device adapter to an initialized SPI NOR device.
 *
 * @param[out] adapter Adapter instance to initialize.
 * @param[in] device SPI NOR device retained by reference.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if either argument is NULL.
 *
 * @pre @p device remains valid while the adapter is in use.
 */
firmware_status_t SpiNorBlockAdapter_Init(
    spi_nor_block_adapter_t *adapter,
    struct spi_nor *device);

/**
 * @brief Get the generic block-device interface owned by an adapter.
 *
 * @param[in] adapter Initialized adapter, or NULL.
 *
 * @return The adapter-owned interface, or NULL when @p adapter is NULL.
 *
 * @note The returned pointer remains valid only while @p adapter remains valid.
 */
const block_device_t *SpiNorBlockAdapter_Interface(
    const spi_nor_block_adapter_t *adapter);

/** Return the bounded asynchronous block-device interface. */
const async_block_device_t *SpiNorBlockAdapter_AsyncInterface(
    const spi_nor_block_adapter_t *adapter);

#endif
