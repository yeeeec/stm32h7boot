/**
 * @file composition.c
 * @brief Composition-root dependency binding implementation.
 */
#include "composition/composition.h"

#include <stddef.h>
#include <string.h>

#include "adapters/cortex_m_application_jump_adapter.h"
#include "adapters/at24_boot_control_adapter.h"
#include "adapters/fatfs_package_source_adapter.h"
#include "adapters/spi_nor_block_adapter.h"
#include "adapters/stm32_clock_adapter.h"
#include "adapters/stm32_qspi_xip_adapter.h"
#include "adapters/uart_log_adapter.h"
#include "bsp/bsp_external_flash.h"
#include "bsp/bsp_eeprom.h"
#include "checksum/crc32_iso_hdlc.h"
#include "crypto/micro_ecc_authenticator.h"
#include "firmware/async_block_device.h"
#include "logging.h"
#include "logging_setup.h"
#include "quadspi.h"
#include "services/capability/slot_policy.h"
#include "services/capability/vector_validation.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/boot_control_service.h"
#include "services/capability/manifest_service.h"
#include "services/use_case/active_validation_service.h"
#include "services/use_case/launch_service.h"

#define SERVICE_IO_BUFFER_SIZE 4096U

static stm32_clock_adapter_t clock_adapter;
static uart_log_adapter_t log_adapter;
static spi_nor_block_adapter_t external_flash_adapter;
static at24_boot_control_adapter_t eeprom_adapter;
static boot_control_service_t boot_control_service;
static micro_ecc_authenticator_t manifest_authenticator;
static manifest_service_t manifest_service;
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
static int manifest_verifier_configured;
static uint8_t manifest_public_key[COMPOSITION_MANIFEST_PUBLIC_KEY_SIZE];
static char manifest_key_id[COMPOSITION_MANIFEST_KEY_ID_MAX_SIZE + 1U];

static int IsManifestKeyIdCharacter(uint8_t value)
{
    return (((value >= 'A') && (value <= 'Z')) ||
            ((value >= 'a') && (value <= 'z')) ||
            ((value >= '0') && (value <= '9')) ||
            (value == '.') || (value == '_') || (value == '-'));
}

firmware_status_t Composition_ConfigureManifestVerifier(
    const uint8_t public_key[COMPOSITION_MANIFEST_PUBLIC_KEY_SIZE],
    const char *key_id)
{
    micro_ecc_authenticator_t probe = {0};
    micro_ecc_authenticator_config_t probe_config;
    size_t key_id_length;
    size_t index;

    if ((public_key == NULL) || (key_id == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((composition_initialized != 0) ||
        (manifest_verifier_configured != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    key_id_length = strlen(key_id);
    if ((key_id_length == 0U) ||
        (key_id_length > COMPOSITION_MANIFEST_KEY_ID_MAX_SIZE))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    for (index = 0U; index < key_id_length; ++index)
    {
        if (!IsManifestKeyIdCharacter((uint8_t)key_id[index]))
        {
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        }
    }
    probe_config.key_id = key_id;
    probe_config.public_key = public_key;
    if (!FirmwareStatus_IsOk(
            MicroEccAuthenticator_Init(&probe, &probe_config)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(manifest_public_key, public_key, sizeof(manifest_public_key));
    memcpy(manifest_key_id, key_id, key_id_length + 1U);
    manifest_verifier_configured = 1;
    return FIRMWARE_STATUS_OK;
}

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

    if (manifest_verifier_configured != 0)
    {
        const micro_ecc_authenticator_config_t authenticator_config = {
            manifest_key_id,
            manifest_public_key,
        };
        manifest_service_dependencies_t manifest_dependencies;

        status = MicroEccAuthenticator_Init(
            &manifest_authenticator, &authenticator_config);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Manifest public key is invalid: %d", (int) status);
            return status;
        }
        manifest_dependencies.authenticator =
            MicroEccAuthenticator_Interface(&manifest_authenticator);
        status = ManifestService_Init(&manifest_service, &manifest_dependencies);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Manifest service init failed: %d", (int) status);
            return status;
        }
    }

    status = At24BootControlAdapter_Init(
        &eeprom_adapter, BSP_EepromDevice());
    if (!FirmwareStatus_IsOk(status)) {
        LOG_ERROR("composition", "EEPROM adapter init failed: %d", (int) status);
        return status;
    }
    {
        boot_control_service_dependencies_t boot_control_dependencies = {
            At24BootControlAdapter_Interface(&eeprom_adapter),
            Crc32IsoHdlc_Interface(&crc32_provider),
        };

        status = BootControlService_Init(
            &boot_control_service, &boot_control_dependencies);
    }
    if (!FirmwareStatus_IsOk(status)) {
        LOG_ERROR("composition", "boot control init failed: %d", (int) status);
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

struct manifest_service *Composition_ManifestService(void)
{
    return (manifest_service.initialized != 0) ? &manifest_service : NULL;
}

int Composition_IsManifestVerifierConfigured(void)
{
    return manifest_verifier_configured;
}
