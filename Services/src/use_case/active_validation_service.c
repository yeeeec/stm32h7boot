/**
 * @file active_validation_service.c
 * @brief Incremental active APP/GUI CRC and vector validation implementation.
 */
#include "services/use_case/active_validation_service.h"

#include <stddef.h>

#include "services/capability/slot_policy.h"

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void Fail(
    active_validation_service_t *service,
    firmware_status_t status,
    boot_error_t error)
{
    service->state = SERVICE_RUN_STATE_FAILED;
    service->result.status = status;
    service->result.error = error;
    service->result.stage = (uint32_t)service->stage;
    service->result.native_error = (int32_t)status;
}

firmware_status_t ActiveValidationService_Init(
    active_validation_service_t *service,
    const active_validation_service_dependencies_t *dependencies)
{
    if ((service == NULL) || (dependencies == NULL) ||
        (dependencies->storage == NULL) ||
        (dependencies->storage->read == NULL) ||
        (dependencies->checksum == NULL) ||
        (dependencies->checksum->reset == NULL) ||
        (dependencies->checksum->update == NULL) ||
        (dependencies->checksum->get_value == NULL) ||
        (dependencies->buffer == NULL) || (dependencies->buffer_size < 8U) ||
        (dependencies->sram_regions == NULL) ||
        (dependencies->sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->storage = dependencies->storage;
    service->checksum = dependencies->checksum;
    service->buffer = dependencies->buffer;
    service->buffer_size = dependencies->buffer_size;
    service->sram_regions = dependencies->sram_regions;
    service->sram_region_count = dependencies->sram_region_count;
    service->state = SERVICE_RUN_STATE_IDLE;
    service->stage = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.status = FIRMWARE_STATUS_OK;
    service->result.error = BOOT_ERROR_NONE;
    service->result.stage = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ActiveValidationService_Start(
    active_validation_service_t *service,
    const boot_active_record_t *active_record)
{
    firmware_status_t status;

    if ((service == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) ||
        (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = SlotPolicy_GetPairLayout(active_record->active_pair, &service->layout);
    if (!FirmwareStatus_IsOk(status) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(
            &service->layout.app, active_record->app_size)) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(
            &service->layout.gui, active_record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    service->active_record = *active_record;
    service->offset = 0U;
    service->stage = ACTIVE_VALIDATION_STAGE_RESET_APP_CRC;
    service->state = SERVICE_RUN_STATE_RUNNING;
    service->result.status = FIRMWARE_STATUS_OK;
    service->result.error = BOOT_ERROR_NONE;
    service->result.stage = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    return FIRMWARE_STATUS_OK;
}

static void ReadComponent(
    active_validation_service_t *service,
    const boot_region_t *region,
    uint32_t image_size,
    int validate_vectors,
    boot_error_t read_error)
{
    uint32_t remaining = image_size - service->offset;
    uint32_t read_size = (remaining < service->buffer_size)
                             ? remaining
                             : service->buffer_size;
    firmware_status_t status = service->storage->read(
        service->storage->context,
        region->flash_offset + service->offset,
        service->buffer,
        read_size);

    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, read_error);
        return;
    }
    if ((validate_vectors != 0) && (service->offset == 0U))
    {
        vector_table_values_t vectors;

        vectors.initial_msp = ReadU32(&service->buffer[0]);
        vectors.reset_handler = ReadU32(&service->buffer[4]);
        status = VectorValidation_Validate(
            &vectors,
            region,
            image_size,
            service->sram_regions,
            service->sram_region_count);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, BOOT_ERROR_VECTOR_TABLE);
            return;
        }
    }
    status = service->checksum->update(
        service->checksum->context, service->buffer, read_size);
    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, BOOT_ERROR_INTERNAL);
        return;
    }

    service->offset += read_size;
}

void ActiveValidationService_Process(active_validation_service_t *service)
{
    firmware_status_t status;
    uint32_t crc;

    if ((service == NULL) ||
        (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case ACTIVE_VALIDATION_STAGE_RESET_APP_CRC:
            status = service->checksum->reset(service->checksum->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage = ACTIVE_VALIDATION_STAGE_READ_APP;
            break;

        case ACTIVE_VALIDATION_STAGE_READ_APP:
            ReadComponent(
                service,
                &service->layout.app,
                service->active_record.app_size,
                1,
                BOOT_ERROR_APP_TARGET_CRC);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.app_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_APP_CRC;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_APP_CRC:
            status = service->checksum->get_value(
                service->checksum->context, &crc);
            if (!FirmwareStatus_IsOk(status) ||
                (crc != service->active_record.app_crc32))
            {
                Fail(
                    service,
                    FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                                : status,
                    BOOT_ERROR_APP_TARGET_CRC);
                break;
            }
            service->stage = ACTIVE_VALIDATION_STAGE_RESET_GUI_CRC;
            break;

        case ACTIVE_VALIDATION_STAGE_RESET_GUI_CRC:
            status = service->checksum->reset(service->checksum->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage = ACTIVE_VALIDATION_STAGE_READ_GUI;
            break;

        case ACTIVE_VALIDATION_STAGE_READ_GUI:
            ReadComponent(
                service,
                &service->layout.gui,
                service->active_record.gui_size,
                0,
                BOOT_ERROR_GUI_TARGET_CRC);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.gui_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_GUI_CRC;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_GUI_CRC:
            status = service->checksum->get_value(
                service->checksum->context, &crc);
            if (!FirmwareStatus_IsOk(status) ||
                (crc != service->active_record.gui_crc32))
            {
                Fail(
                    service,
                    FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                                : status,
                    BOOT_ERROR_GUI_TARGET_CRC);
                break;
            }
            service->state = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status = FIRMWARE_STATUS_OK;
            service->result.error = BOOT_ERROR_NONE;
            service->result.stage = (uint32_t)service->stage;
            service->result.native_error = 0;
            service->stage = ACTIVE_VALIDATION_STAGE_IDLE;
            break;

        default:
            Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INTERNAL);
            break;
    }
}

service_run_state_t ActiveValidationService_GetState(
    const active_validation_service_t *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *ActiveValidationService_GetResult(
    const active_validation_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
