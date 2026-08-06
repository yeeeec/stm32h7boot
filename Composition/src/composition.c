/**
 * @file composition.c
 * @brief Composition-root dependency binding implementation.
 */
#include "composition/composition.h"

#include "adapters/cortex_m_application_jump_adapter.h"
#include "adapters/fatfs_package_source_adapter.h"
#include "adapters/spi_nor_block_adapter.h"
#include "adapters/stm32_clock_adapter.h"
#include "adapters/stm32_qspi_xip_adapter.h"
#include "adapters/uart_log_adapter.h"
#include "bsp/bsp_external_flash.h"
#include "checksum/crc32_iso_hdlc.h"
#include "firmware/async_block_device.h"
#include "logging.h"
#include "logging_setup.h"
#include "quadspi.h"
#include "services/capability/slot_policy.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/active_validation_service.h"
#include "services/use_case/launch_service.h"

#define SERVICE_IO_BUFFER_SIZE 4096U

static stm32_clock_adapter_t clock_adapter;
static uart_log_adapter_t log_adapter;
static spi_nor_block_adapter_t external_flash_adapter;
static fatfs_package_source_adapter_t package_source_adapter;
static stm32_qspi_xip_adapter_t xip_adapter;
static cortex_m_application_jump_adapter_t jump_adapter;
static crc32_iso_hdlc_t crc32_provider;
static active_validation_service_t active_validation_service;
static launch_service_t launch_service;
static uint8_t service_io_buffer[SERVICE_IO_BUFFER_SIZE]
    __attribute__((section(".ram_d2"), aligned(32)));
static const memory_region_t application_sram_regions[] = {
    {0x20000000UL, 128UL * 1024UL},
    {0x24000000UL, 512UL * 1024UL},
    {0x30000000UL, 288UL * 1024UL},
    {0x38000000UL, 64UL * 1024UL},
};
static int composition_initialized;

firmware_status_t Composition_Init(void) {
    firmware_status_t status;
    const async_block_device_t *external_flash;
    async_block_device_info_t external_flash_info;
    active_validation_service_dependencies_t validation_dependencies;
    launch_service_dependencies_t launch_dependencies;

    if (composition_initialized != 0) {
        LOG_WARN("composition", "initialization requested more than once");
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* Construct all concrete adapters before exposing their interfaces. */
    STM32ClockAdapter_Init(&clock_adapter);
    UartLogAdapter_Init(&log_adapter);

    status = Logging_Configure(UartLogAdapter_Interface(&log_adapter),
                               STM32ClockAdapter_Interface(&clock_adapter));
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }
    LOG_DEBUG("composition", "logger dependencies bound");

    status = SpiNorBlockAdapter_Init(&external_flash_adapter, BSP_ExternalFlashDevice());
    if (!FirmwareStatus_IsOk(status)) {
        LOG_ERROR("composition", "external flash adapter init failed: %d", (int) status);
        return status;
    }
    /* Validate device geometry before making storage available to services. */
    external_flash = SpiNorBlockAdapter_AsyncInterface(&external_flash_adapter);
    status = external_flash->get_info(external_flash->context, &external_flash_info);
    if (!FirmwareStatus_IsOk(status)) {
        LOG_ERROR("composition", "external flash info failed: %d", (int) status);
        return status;
    }
    if ((external_flash_info.capacity_bytes == 0U) ||
        (external_flash_info.program_size == 0U) ||
        (external_flash_info.erase_size == 0U)) {
        LOG_ERROR("composition", "external flash geometry is invalid");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = SlotPolicy_ValidateStorageGeometry(
        external_flash_info.capacity_bytes, external_flash_info.erase_size);
    if (!FirmwareStatus_IsOk(status)) {
        LOG_ERROR("composition", "external flash does not match slot policy");
        return status;
    }

    status = Crc32IsoHdlc_Init(&crc32_provider);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }
    status = FatFsPackageSourceAdapter_Init(&package_source_adapter);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }
    status = Stm32QspiXipAdapter_Init(&xip_adapter, &hqspi);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }
    status = CortexMApplicationJumpAdapter_Init(&jump_adapter);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }

    validation_dependencies.storage = external_flash;
    validation_dependencies.checksum = Crc32IsoHdlc_Interface(&crc32_provider);
    validation_dependencies.buffer = service_io_buffer;
    validation_dependencies.buffer_size = sizeof(service_io_buffer);
    validation_dependencies.sram_regions = application_sram_regions;
    validation_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = ActiveValidationService_Init(
        &active_validation_service, &validation_dependencies);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }

    launch_dependencies.storage = external_flash;
    launch_dependencies.xip_controller =
        Stm32QspiXipAdapter_Interface(&xip_adapter);
    launch_dependencies.application_jump =
        CortexMApplicationJumpAdapter_Interface(&jump_adapter);
    launch_dependencies.sram_regions = application_sram_regions;
    launch_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = LaunchService_Init(&launch_service, &launch_dependencies);
    if (!FirmwareStatus_IsOk(status)) {
        return status;
    }

    LOG_INFO("storage", "external flash ready: capacity=%lu, write=%lu, erase=%lu bytes",
             (unsigned long) external_flash_info.capacity_bytes,
             (unsigned long) external_flash_info.program_size,
             (unsigned long) external_flash_info.erase_size);

    composition_initialized = 1;
    LOG_INFO("composition", "service dependencies initialized");
    return FIRMWARE_STATUS_OK;
}

int Composition_IsInitialized(void) {
    return composition_initialized;
}
