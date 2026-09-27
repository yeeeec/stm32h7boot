#include "runtime_verifier.h"

#include <string.h>

#include "crypto/sha256.h"
#include "firmware/memory.h"
#include "logging.h"
#include "platform/platform_cpu.h"
#include "platform/platform_flash.h"
#include "platform/platform_memory_map.h"
#include "platform/platform_system.h"
#include "platform/platform_therapy.h"
#include "update/component_registry.h"
#include "update_package.h"

#define RUNTIME_VERIFY_BLOCK_SIZE 4096U

static FIRMWARE_STORAGE_RAM uint8_t s_verify_buffer[RUNTIME_VERIFY_BLOCK_SIZE];

static firmware_status_t read_target(image_target_t target, uint32_t address, void *buffer,
                                     uint32_t size)
{
    return target == IMAGE_TARGET_THERAPY ? PlatformTherapy_Read(address, buffer, size)
                                          : PlatformFlash_Read(address, buffer, size);
}

static firmware_status_t verify_component(const update_manifest_component_t *component)
{
    const update_component_descriptor_t *descriptor;
    crypto_sha256_context_t hash;
    uint8_t expected[CRYPTO_SHA256_DIGEST_SIZE];
    uint8_t actual[CRYPTO_SHA256_DIGEST_SIZE];
    uint32_t offset = 0U;
    firmware_status_t status;

    descriptor = UpdateComponent_FindByTarget(component->target);
    if (descriptor == NULL || component->size > descriptor->maximum_size ||
        !UpdateHex_DecodeSha256(component->sha256, expected))
        return FIRMWARE_STATUS_INVALID_STATE;
    if (Crypto_Sha256Init(&hash) != 0)
        return FIRMWARE_STATUS_IO_ERROR;

    while (offset < component->size)
    {
        uint32_t chunk = component->size - offset;
        if (chunk > sizeof(s_verify_buffer))
            chunk = sizeof(s_verify_buffer);
        status = read_target(component->target, descriptor->address + offset, s_verify_buffer,
                             chunk);
        if (FirmwareStatus_IsError(status) || Crypto_Sha256Update(&hash, s_verify_buffer, chunk) != 0)
        {
            Crypto_Sha256Abort(&hash);
            return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_IO_ERROR;
        }
        offset += chunk;
        PlatformSystem_WatchdogRefresh();
    }
    if (Crypto_Sha256Finish(&hash, actual) != 0)
        return FIRMWARE_STATUS_IO_ERROR;
    if (memcmp(expected, actual, sizeof(actual)) != 0)
    {
        LOG_ERROR("runtime", "component hash mismatch: name=%s target=%u expected=%s",
                  component->name, (unsigned) component->target, component->sha256);
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RuntimeVerifier_Validate(void)
{
    const volatile uint32_t *vectors;
    uint32_t address;
    uint32_t stack_pointer;
    uint32_t reset_handler_raw;
    uint32_t reset_handler;
    firmware_status_t status;

    status = PlatformFlash_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformFlash_EnterMemoryMapped();
    if (FirmwareStatus_IsError(status))
        return status;
    address = PLATFORM_QSPI_MAPPED_BASE + PLATFORM_APP_OFFSET;
    if ((address & (PLATFORM_APP_VECTOR_ALIGNMENT - 1U)) != 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    vectors           = (const volatile uint32_t *) (uintptr_t) address;
    stack_pointer     = vectors[0];
    reset_handler_raw = vectors[1];
    reset_handler     = reset_handler_raw & ~1UL;
    if (stack_pointer == UINT32_MAX || reset_handler_raw == UINT32_MAX ||
        !PlatformCpu_IsValidStackPointer(stack_pointer) || (stack_pointer & 7U) != 0U ||
        (reset_handler_raw & 1U) == 0U ||
        reset_handler < address || reset_handler >= address + PLATFORM_APP_MAX_SIZE)
        return FIRMWARE_STATUS_INVALID_STATE;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RuntimeVerifier_VerifyPackage(const update_package_t *package)
{
    size_t index;
    firmware_status_t status;
    int therapy_started = 0;

    if (package == NULL || package->manifest.component_count == 0U ||
        package->manifest.component_mask != UpdateComponent_RequiredMask())
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    /* RuntimeVerifier_Validate leaves QSPI mapped; hash reads use commands. */
    status = PlatformFlash_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformFlash_ExitMemoryMapped();
    if (FirmwareStatus_IsError(status))
        return status;

    for (index = 0U; index < package->manifest.component_count; ++index)
    {
        if (package->manifest.components[index].target == IMAGE_TARGET_THERAPY)
        {
            status = PlatformTherapy_Init();
            if (FirmwareStatus_IsError(status))
                return status;
            status = PlatformTherapy_BeginRead();
            if (FirmwareStatus_IsError(status))
                return status;
            therapy_started = 1;
            break;
        }
    }

    for (index = 0U; index < package->manifest.component_count; ++index)
    {
        status = verify_component(&package->manifest.components[index]);
        if (FirmwareStatus_IsError(status))
            break;
    }

    if (therapy_started != 0)
    {
        firmware_status_t end_status = PlatformTherapy_EndRead();
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(end_status))
            status = end_status;
    }
    return status;
}
