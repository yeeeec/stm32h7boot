#include "composition/composition.h"

#include "adapters/stm32_clock_adapter.h"
#include "adapters/stm32_watchdog_adapter.h"
#include "adapters/spi_nor_block_adapter.h"
#include "adapters/uart_log_adapter.h"
#include "application/application.h"
#include "bsp/bsp_external_flash.h"
#include "services/runtime_service.h"

static stm32_clock_adapter_t clock_adapter;
static stm32_watchdog_adapter_t watchdog_adapter;
static uart_log_adapter_t log_adapter;
static spi_nor_block_adapter_t external_flash_adapter;
static runtime_service_t runtime_service;
static int composition_initialized;

firmware_status_t Composition_Init(void)
{
    firmware_status_t status;
    runtime_service_dependencies_t dependencies;
    block_device_info_t external_flash_info;

    if (composition_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    STM32ClockAdapter_Init(&clock_adapter);
    STM32WatchdogAdapter_Init(&watchdog_adapter);
    UartLogAdapter_Init(&log_adapter);

    status = SpiNorBlockAdapter_Init(
        &external_flash_adapter, BSP_ExternalFlashDevice());
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = SpiNorBlockAdapter_Interface(&external_flash_adapter)->get_info(
        SpiNorBlockAdapter_Interface(&external_flash_adapter)->context,
        &external_flash_info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((external_flash_info.capacity_bytes == 0U) ||
        (external_flash_info.write_size == 0U) ||
        (external_flash_info.erase_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    dependencies.clock = STM32ClockAdapter_Interface(&clock_adapter);
    dependencies.watchdog = STM32WatchdogAdapter_Interface(&watchdog_adapter);
    dependencies.log = UartLogAdapter_Interface(&log_adapter);

    status = RuntimeService_Init(&runtime_service, &dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = Application_Configure(&runtime_service);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    composition_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

int Composition_IsInitialized(void)
{
    return composition_initialized;
}
