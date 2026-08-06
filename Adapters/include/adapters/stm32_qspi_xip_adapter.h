/**
 * @file stm32_qspi_xip_adapter.h
 * @brief STM32 QSPI memory-mapped execution adapter.
 */
#ifndef ADAPTERS_STM32_QSPI_XIP_ADAPTER_H
#define ADAPTERS_STM32_QSPI_XIP_ADAPTER_H

#include "firmware/xip_controller.h"

/** Adapter-owned XIP interface and memory-mapped state. */
typedef struct
{
    xip_controller_t interface;
    void *qspi_handle;
    int mapped;
} stm32_qspi_xip_adapter_t;

/**
 * @brief Bind an initialized STM32 QSPI handle to the XIP interface.
 *
 * @param[out] adapter Adapter object to initialize.
 * @param[in] qspi_handle Initialized HAL QSPI handle retained by reference.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if an argument is NULL.
 */
firmware_status_t Stm32QspiXipAdapter_Init(
    stm32_qspi_xip_adapter_t *adapter,
    void *qspi_handle);

/** Return the XIP controller interface owned by an adapter. */
const xip_controller_t *Stm32QspiXipAdapter_Interface(
    const stm32_qspi_xip_adapter_t *adapter);

#endif
