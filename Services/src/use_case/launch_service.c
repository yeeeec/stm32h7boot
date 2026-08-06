/**
 * @file launch_service.c
 * @brief XIP launch orchestration implementation.
 */
#include "services/use_case/launch_service.h"

#include <stddef.h>

#include "services/capability/slot_policy.h"

typedef enum
{
    LAUNCH_STAGE_IDLE = 0,
    LAUNCH_STAGE_READ_VECTOR,
    LAUNCH_STAGE_VALIDATE_VECTOR,
    LAUNCH_STAGE_ENTER_XIP,
    LAUNCH_STAGE_INVALIDATE_CACHE,
    LAUNCH_STAGE_JUMP
} launch_stage_t;

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static firmware_status_t Fail(launch_service_t *service, firmware_status_t status,
                              boot_error_t error, launch_stage_t stage)
{
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) stage;
    service->result.native_error = (int32_t) status;
    return status;
}

firmware_status_t LaunchService_Init(launch_service_t *service,
                                     const launch_service_dependencies_t *dependencies)
{
    if ((service == NULL) || (dependencies == NULL) || (dependencies->storage == NULL) ||
        (dependencies->storage->read == NULL) || (dependencies->xip_controller == NULL) ||
        (dependencies->xip_controller->enter_memory_mapped_read == NULL) ||
        (dependencies->xip_controller->is_memory_mapped == NULL) ||
        (dependencies->xip_controller->invalidate_mapped_cache == NULL) ||
        (dependencies->application_jump == NULL) ||
        (dependencies->application_jump->execute == NULL) || (dependencies->sram_regions == NULL) ||
        (dependencies->sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->storage             = dependencies->storage;
    service->xip_controller      = dependencies->xip_controller;
    service->application_jump    = dependencies->application_jump;
    service->sram_regions        = dependencies->sram_regions;
    service->sram_region_count   = dependencies->sram_region_count;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = LAUNCH_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t LaunchService_Execute(launch_service_t *service,
                                        const boot_active_record_t *active_record)
{
    boot_pair_layout_t layout;
    vector_table_values_t vectors;
    uint8_t vector_bytes[8];
    firmware_status_t status;
    int mapped;

    if ((service == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = SlotPolicy_GetPairLayout(active_record->active_pair, &layout);
    if (!FirmwareStatus_IsOk(status) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.app, active_record->app_size)))
    {
        return Fail(service, FIRMWARE_STATUS_OUT_OF_RANGE, BOOT_ERROR_CONTROL_RECORD,
                    LAUNCH_STAGE_READ_VECTOR);
    }
    status = service->xip_controller->is_memory_mapped(service->xip_controller->context, &mapped);
    if (!FirmwareStatus_IsOk(status) || (mapped != 0))
    {
        return Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                    BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_READ_VECTOR);
    }
    status = service->storage->read(service->storage->context, layout.app.flash_offset,
                                    vector_bytes, sizeof(vector_bytes));
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_MEDIA_UNAVAILABLE, LAUNCH_STAGE_READ_VECTOR);
    }

    vectors.initial_msp   = ReadU32(&vector_bytes[0]);
    vectors.reset_handler = ReadU32(&vector_bytes[4]);
    status = VectorValidation_Validate(&vectors, &layout.app, active_record->app_size,
                                       service->sram_regions, service->sram_region_count);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_VECTOR_TABLE, LAUNCH_STAGE_VALIDATE_VECTOR);
    }

    status = service->xip_controller->enter_memory_mapped_read(service->xip_controller->context);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_ENTER_XIP);
    }
    status = service->xip_controller->invalidate_mapped_cache(
        service->xip_controller->context, layout.app.mapped_address, active_record->app_size);
    if (!FirmwareStatus_IsOk(status))
    {
        return Fail(service, status, BOOT_ERROR_XIP_SETUP, LAUNCH_STAGE_INVALIDATE_CACHE);
    }

    status = service->application_jump->execute(service->application_jump->context,
                                                layout.app.mapped_address);
    return Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                BOOT_ERROR_INTERNAL, LAUNCH_STAGE_JUMP);
}

const service_result_t *LaunchService_GetResult(const launch_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
