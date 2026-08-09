/**
 * @file composition.c
 * @brief 生产 Firmware 镜像的静态依赖图。
 *
 * Composition Root 持有所有 Adapter 和 Service 实例，将 BSP 设备转换为面向
 * Architecture 的接口，并向 Application 提供最终依赖图。本模块只包含 wiring
 * 和不变的 Board Policy；Boot、Update、Validation、Launch 决策仍由所属层负责。
 *
 * 只有所有依赖成功初始化后，才通过 Application_Configure() 发布依赖图。初始化
 * 没有 rollback，因此任何失败都是当前 Boot 的终态，并且必须保持
 * Composition_IsInitialized() 为 false。
 */
#include "composition/composition.h"

#include <stddef.h>

#include "adapters/at24_boot_control_adapter.h"
#include "adapters/cortex_m_application_jump_adapter.h"
#include "adapters/fatfs_package_source_adapter.h"
#include "adapters/fatfs_update_request_store_adapter.h"
#include "adapters/spi_nor_block_adapter.h"
#include "adapters/stm32_clock_adapter.h"
#include "adapters/stm32_qspi_xip_adapter.h"
#include "adapters/stm32_system_reset_adapter.h"
#include "adapters/uart_log_adapter.h"
#include "application/application.h"
#include "application/application_config.h"
#include "bsp/bsp_eeprom.h"
#include "bsp/bsp_external_flash.h"
#include "checksum/crc32_iso_hdlc.h"
#include "crypto/sha256.h"
#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "logging.h"
#include "logging_setup.h"
#include "quadspi.h"
#include "services/capability/boot_control_service.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/manifest_service.h"
#include "services/capability/update_request_service.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/active_validation_service.h"
#include "services/use_case/launch_service.h"
#include "services/use_case/update_service.h"

#define SERVICE_IO_BUFFER_SIZE 4096U

static stm32_clock_adapter_t clock_adapter;
static uart_log_adapter_t log_adapter;
static spi_nor_block_adapter_t external_flash_adapter;
static at24_boot_control_adapter_t eeprom_adapter;
static boot_control_service_t boot_control_service;
static sha256_context_t manifest_hash_context;
static sha256_context_t active_validation_hash_context;
static manifest_service_t manifest_service;
static update_request_service_t update_request_service;
static fatfs_release_volume_context_t release_volume;
static fatfs_package_source_adapter_t package_source_adapter;
static fatfs_update_request_store_adapter_t request_store_adapter;
static stm32_qspi_xip_adapter_t xip_adapter;
static cortex_m_application_jump_adapter_t jump_adapter;
static stm32_system_reset_adapter_t system_reset_adapter;
static crc32_iso_hdlc_t crc32_provider;
static active_validation_service_t active_validation_service;
static launch_service_t launch_service;
static update_service_t update_service;

/*
 * Service 工作区具有静态生命周期。放置到 D2 可将大 Buffer 移出主 SRAM，
 * Cache-line 对齐则满足需要 DMA/Cache maintenance 的 Consumer Contract。
 */
static uint8_t manifest_buffer[UPDATE_SERVICE_MANIFEST_MAX_SIZE]
    __attribute__((section(".ram_d2"), aligned(32)));
static uint8_t service_io_buffer[SERVICE_IO_BUFFER_SIZE]
    __attribute__((section(".ram_d2"), aligned(32)));

/*
 * 该表是可执行镜像的策略，不是对所有可写 RAM 的探测。Vector Validation
 * 必须拒绝落在这些区域之外的初始 Stack Pointer。
 */
static const memory_region_t application_sram_regions[] = {
    {0x20000000UL, 128UL * 1024UL},
    {0x24000000UL, 512UL * 1024UL},
    {0x30000000UL, 288UL * 1024UL},
    {0x38000000UL, 64UL * 1024UL},
};
static int composition_initialized;
static firmware_status_t ManifestHashReset(void *context)
{
    return Sha256_Reset((sha256_context_t *) context);
}

static firmware_status_t ManifestHashUpdate(void *context, const void *data, size_t size)
{
    return Sha256_Update((sha256_context_t *) context, data, size);
}

static firmware_status_t ManifestHashFinish(void *context,
                                            uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    return Sha256_Finish((sha256_context_t *) context, digest);
}

static hash_provider_t manifest_hash_interface = {
    &manifest_hash_context,
    ManifestHashReset,
    ManifestHashUpdate,
    ManifestHashFinish,
};

/*
 * Active Runtime Validation 使用独立的可变 Hash Context，避免其 Digest 生命周期
 * 破坏 Manifest/Update Request 的 Hash 状态。
 */
static hash_provider_t active_validation_hash_interface = {
    &active_validation_hash_context,
    ManifestHashReset,
    ManifestHashUpdate,
    ManifestHashFinish,
};

firmware_status_t Composition_Init(void)
{
    firmware_status_t status;
    const async_block_device_t *external_flash;
    async_block_device_info_t external_flash_info;
    active_validation_service_dependencies_t validation_dependencies;
    launch_service_dependencies_t launch_dependencies;
    update_service_dependencies_t update_dependencies;

    if (composition_initialized != 0)
    {
        LOG_WARN("composition", "initialization requested more than once");
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 先配置 Logger，保证后续依赖失败仍可诊断。 */
    STM32ClockAdapter_Init(&clock_adapter);
    UartLogAdapter_Init(&log_adapter);

    status = Logging_Configure(UartLogAdapter_Interface(&log_adapter),
                               STM32ClockAdapter_Interface(&clock_adapter));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    LOG_DEBUG("composition", "logger dependencies bound");

    status = SpiNorBlockAdapter_Init(&external_flash_adapter, BSP_ExternalFlashDevice());
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "external flash adapter init failed: %d", (int) status);
        return status;
    }

    /* 几何参数无效时必须在任何 Service 修改 Runtime 前失败。 */
    external_flash = SpiNorBlockAdapter_AsyncInterface(&external_flash_adapter);
    status         = external_flash->get_info(external_flash->context, &external_flash_info);
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "external flash info failed: %d", (int) status);
        return status;
    }
    if ((external_flash_info.capacity_bytes == 0U) || (external_flash_info.program_size == 0U) ||
        (external_flash_info.erase_size == 0U))
    {
        LOG_ERROR("composition", "external flash geometry is invalid");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = Crc32IsoHdlc_Init(&crc32_provider);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    {
        manifest_service_dependencies_t manifest_dependencies = {&manifest_hash_interface};

        status = ManifestService_Init(&manifest_service, &manifest_dependencies);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Manifest service init failed: %d", (int) status);
            return status;
        }
    }

    {
        update_request_service_dependencies_t request_dependencies = {&manifest_hash_interface};

        status = UpdateRequestService_Init(&update_request_service, &request_dependencies);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Update request service init failed: %d", (int)status);
            return status;
        }
    }

    status = At24BootControlAdapter_Init(&eeprom_adapter, BSP_EepromDevice());
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "EEPROM adapter init failed: %d", (int) status);
        return status;
    }
    {
        boot_control_service_dependencies_t boot_control_dependencies = {
            At24BootControlAdapter_Interface(&eeprom_adapter),
            Crc32IsoHdlc_Interface(&crc32_provider),
        };

        status = BootControlService_Init(&boot_control_service, &boot_control_dependencies);
    }
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "boot control init failed: %d", (int) status);
        return status;
    }

    status = FatFsReleaseVolumeContext_Init(&release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = FatFsPackageSourceAdapter_Init(&package_source_adapter, &release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = FatFsUpdateRequestStoreAdapter_Init(&request_store_adapter, &release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Stm32QspiXipAdapter_Init(&xip_adapter, &hqspi);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = CortexMApplicationJumpAdapter_Init(&jump_adapter);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Stm32SystemResetAdapter_Init(&system_reset_adapter);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    validation_dependencies.storage      = external_flash;
    validation_dependencies.hash         = &active_validation_hash_interface;
    validation_dependencies.buffer       = service_io_buffer;
    validation_dependencies.buffer_size  = sizeof(service_io_buffer);
    validation_dependencies.sram_regions = application_sram_regions;
    validation_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = ActiveValidationService_Init(&active_validation_service, &validation_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    launch_dependencies.storage          = external_flash;
    launch_dependencies.xip_controller   = Stm32QspiXipAdapter_Interface(&xip_adapter);
    launch_dependencies.application_jump = CortexMApplicationJumpAdapter_Interface(&jump_adapter);
    launch_dependencies.sram_regions     = application_sram_regions;
    launch_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = LaunchService_Init(&launch_service, &launch_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    update_dependencies.package_source       =
        FatFsPackageSourceAdapter_Interface(&package_source_adapter);
    update_dependencies.manifest_service     = &manifest_service;
    update_dependencies.update_request_service = &update_request_service;
    update_dependencies.hash                 = &manifest_hash_interface;
    update_dependencies.storage              = external_flash;
    /*
     * 安装期间由 Update Service 独占 XIP 状态切换，并必须在首次 Runtime
     * 擦除前肯定确认已处于 indirect mode。
     */
    update_dependencies.xip_controller       =
        Stm32QspiXipAdapter_Interface(&xip_adapter);
    update_dependencies.runtime_layout       = BootRuntimeLayout_Get();
    update_dependencies.manifest_buffer      = manifest_buffer;
    update_dependencies.manifest_buffer_size = sizeof(manifest_buffer);
    update_dependencies.io_buffer            = service_io_buffer;
    update_dependencies.io_buffer_size       = sizeof(service_io_buffer);
    status = UpdateService_Init(&update_service, &update_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    {
        const application_dependencies_t application_dependencies = {
            .boot_control       = &boot_control_service,
            .update             = &update_service,
            .validation         = &active_validation_service,
            .launch             = &launch_service,
            .package_source     = FatFsPackageSourceAdapter_Interface(&package_source_adapter),
            .update_request_store =
                FatFsUpdateRequestStoreAdapter_Interface(&request_store_adapter),
            .update_request_service = &update_request_service,
            .system_reset       = Stm32SystemResetAdapter_Interface(&system_reset_adapter),
            .bootloader_version = {1U, 0U, 0U},
        };

        status = Application_Configure(&application_dependencies);
    }
    if (!FirmwareStatus_IsOk(status))
    {
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

int Composition_IsInitialized(void)
{
    return composition_initialized;
}
