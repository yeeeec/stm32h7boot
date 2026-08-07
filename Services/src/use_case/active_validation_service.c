/**
 * @file active_validation_service.c
 * @brief Incremental fixed-runtime APP/GUI SHA-256 and vector validation.
 */
#include "services/use_case/active_validation_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
static const char *ActiveValidationStageName(active_validation_stage_t stage)
{
    switch (stage)
    {
        case ACTIVE_VALIDATION_STAGE_IDLE:
            return "idle";
        case ACTIVE_VALIDATION_STAGE_RESET_APP_HASH:
            return "reset-app-hash";
        case ACTIVE_VALIDATION_STAGE_READ_APP:
            return "read-app";
        case ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH:
            return "finish-app-hash";
        case ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH:
            return "reset-gui-hash";
        case ACTIVE_VALIDATION_STAGE_READ_GUI:
            return "read-gui";
        case ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH:
            return "finish-gui-hash";
        default:
            return "unknown";
    }
}

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static void Fail(active_validation_service_t *service, firmware_status_t status, boot_error_t error)
{
    LOG_ERROR("active", "failed: status=%d error=%d stage=%s", (int) status, (int) error,
              ActiveValidationStageName(service->stage));
    service->state               = SERVICE_RUN_STATE_FAILED;
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = (int32_t) status;
}

firmware_status_t
ActiveValidationService_Init(active_validation_service_t *service,
                             const active_validation_service_dependencies_t *dependencies)
{
    if ((service == NULL) || (dependencies == NULL) || (dependencies->storage == NULL) ||
        (dependencies->storage->read == NULL) || (dependencies->hash == NULL) ||
        (dependencies->hash->reset == NULL) || (dependencies->hash->update == NULL) ||
        (dependencies->hash->finish == NULL) || (dependencies->buffer == NULL) ||
        (dependencies->buffer_size < 8U) || (dependencies->sram_regions == NULL) ||
        (dependencies->sram_region_count == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->storage             = dependencies->storage;
    service->hash                = dependencies->hash;
    service->buffer              = dependencies->buffer;
    service->buffer_size         = dependencies->buffer_size;
    service->sram_regions        = dependencies->sram_regions;
    service->sram_region_count   = dependencies->sram_region_count;
    service->state               = SERVICE_RUN_STATE_IDLE;
    service->stage               = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ActiveValidationService_Start(active_validation_service_t *service,
                                                const boot_active_record_t *active_record)
{
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();

    if ((service == NULL) || (active_record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    if ((active_record->app_size == 0U) || (active_record->app_size > layout->app_max_size) ||
        (active_record->gui_size == 0U) || (active_record->gui_size > layout->gui_max_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    service->active_record       = *active_record;
    service->layout              = layout;
    service->offset              = 0U;
    service->stage               = ACTIVE_VALIDATION_STAGE_RESET_APP_HASH;
    service->state               = SERVICE_RUN_STATE_RUNNING;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = ACTIVE_VALIDATION_STAGE_IDLE;
    service->result.native_error = 0;
    LOG_INFO("active", "started: app=%lu gui=%lu", (unsigned long) active_record->app_size,
             (unsigned long) active_record->gui_size);
    return FIRMWARE_STATUS_OK;
}

static void ReadComponent(active_validation_service_t *service, const boot_region_t *region,
                          uint32_t image_size, int validate_vectors, boot_error_t read_error)
{
    uint32_t remaining = image_size - service->offset;
    uint32_t read_size = (remaining < service->buffer_size) ? remaining : service->buffer_size;
    firmware_status_t status =
        service->storage->read(service->storage->context, region->flash_offset + service->offset,
                               service->buffer, read_size);

    if (!FirmwareStatus_IsOk(status))
    {
        Fail(service, status, read_error);
        return;
    }
    if ((validate_vectors != 0) && (service->offset == 0U))
    {
        vector_table_values_t vectors;

        vectors.initial_msp   = ReadU32(&service->buffer[0]);
        vectors.reset_handler = ReadU32(&service->buffer[4]);
        LOG_DEBUG("active", "vector read: msp=0x%08lx reset=0x%08lx",
                  (unsigned long) vectors.initial_msp, (unsigned long) vectors.reset_handler);
        status = VectorValidation_Validate(&vectors, region, image_size, service->sram_regions,
                                           service->sram_region_count);
        if (!FirmwareStatus_IsOk(status))
        {
            Fail(service, status, BOOT_ERROR_VECTOR_TABLE);
            return;
        }
    }
    status = service->hash->update(service->hash->context, service->buffer, read_size);
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
    boot_region_t app_region;
    boot_region_t gui_region;
    uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE];

    if ((service == NULL) || (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case ACTIVE_VALIDATION_STAGE_RESET_APP_HASH:
            status = service->hash->reset(service->hash->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage  = ACTIVE_VALIDATION_STAGE_READ_APP;
            LOG_INFO("active", "app hash scan started: size=%lu",
                     (unsigned long) service->active_record.app_size);
            break;

        case ACTIVE_VALIDATION_STAGE_READ_APP:
            app_region = (boot_region_t) {
                service->layout->app_offset,
                service->layout->app_xip_base,
                service->layout->app_max_size,
            };
            ReadComponent(service, &app_region, service->active_record.app_size, 1,
                          BOOT_ERROR_APP_TARGET_CRC);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.app_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH:
            status = service->hash->finish(service->hash->context, digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(digest, service->active_record.app_sha256, sizeof(digest)) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                     BOOT_ERROR_APP_TARGET_CRC);
                break;
            }
            LOG_INFO("active", "app hash verified");
            service->stage = ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH;
            break;

        case ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH:
            status = service->hash->reset(service->hash->context);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status, BOOT_ERROR_INTERNAL);
                break;
            }
            service->offset = 0U;
            service->stage  = ACTIVE_VALIDATION_STAGE_READ_GUI;
            LOG_INFO("active", "gui hash scan started: size=%lu",
                     (unsigned long) service->active_record.gui_size);
            break;

        case ACTIVE_VALIDATION_STAGE_READ_GUI:
            gui_region = (boot_region_t) {
                service->layout->gui_offset,
                service->layout->gui_mmap_base,
                service->layout->gui_max_size,
            };
            ReadComponent(service, &gui_region, service->active_record.gui_size, 0,
                          BOOT_ERROR_GUI_TARGET_CRC);
            if ((service->state == SERVICE_RUN_STATE_RUNNING) &&
                (service->offset == service->active_record.gui_size))
            {
                service->stage = ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH;
            }
            break;

        case ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH:
            status = service->hash->finish(service->hash->context, digest);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(digest, service->active_record.gui_sha256, sizeof(digest)) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE : status,
                     BOOT_ERROR_GUI_TARGET_CRC);
                break;
            }
            LOG_INFO("active", "gui hash verified");
            service->state               = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status       = FIRMWARE_STATUS_OK;
            service->result.error        = BOOT_ERROR_NONE;
            service->result.stage        = (uint32_t) service->stage;
            service->result.native_error = 0;
            service->stage               = ACTIVE_VALIDATION_STAGE_IDLE;
            break;

        default:
            Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INTERNAL);
            break;
    }
}

service_run_state_t ActiveValidationService_GetState(const active_validation_service_t *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *
ActiveValidationService_GetResult(const active_validation_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
