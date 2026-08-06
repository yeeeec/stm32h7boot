/**
 * @file boot_control_service.c
 * @brief Power-loss-safe Boot Control A/B record implementation.
 */
#include "services/capability/boot_control_service.h"

#include <stddef.h>
#include <string.h>

#include "services/capability/slot_policy.h"

#define ACTIVE_RECORD_A_ADDRESS  0x0000U
#define ACTIVE_RECORD_B_ADDRESS  0x0100U
#define ACTIVE_RECORD_SIZE       256U
#define ACTIVE_RECORD_CRC_OFFSET 0x00F8U
#define ACTIVE_RECORD_MARKER     0x00FCU

#define REQUEST_RECORD_A_ADDRESS  0x0200U
#define REQUEST_RECORD_B_ADDRESS  0x0240U
#define REQUEST_RECORD_SIZE       64U
#define REQUEST_RECORD_CRC_OFFSET 0x0038U
#define REQUEST_RECORD_MARKER     0x003CU

#define ACTIVE_RECORD_MAGIC  0x52434248UL
#define REQUEST_RECORD_MAGIC 0x51524248UL
#define COMMIT_MARKER        0x434F4D54UL
#define INVALID_MARKER       0xFFFFFFFFUL
#define RECORD_FORMAT_V1     1U
#define ACTIVE_VALID_STATE   1U
#define RECORD_SLOT_NONE     (-1)
#define RECORD_SLOT_CONFLICT (-2)

typedef enum
{
    RECORD_KIND_ACTIVE = 0,
    RECORD_KIND_REQUEST
} record_kind_t;

static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static firmware_status_t CalculateCrc(
    boot_control_service_t *service,
    const uint8_t *data,
    uint32_t size,
    uint32_t *crc)
{
    firmware_status_t status = service->checksum->reset(
        service->checksum->context);

    if (FirmwareStatus_IsOk(status))
    {
        status = service->checksum->update(
            service->checksum->context, data, size);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = service->checksum->get_value(
            service->checksum->context, crc);
    }
    return status;
}

static int SequenceIsNewer(uint32_t candidate, uint32_t reference)
{
    uint32_t difference = candidate - reference;

    return (difference != 0U) && (difference < 0x80000000UL);
}

static int MarkerFitsPage(
    uint32_t record_address,
    uint32_t marker_offset,
    uint32_t page_size)
{
    uint32_t page_offset = (record_address + marker_offset) % page_size;

    return page_offset <= (page_size - sizeof(uint32_t));
}

static firmware_status_t ValidateActiveBuffer(
    boot_control_service_t *service,
    const uint8_t *buffer,
    boot_active_record_t *record)
{
    boot_pair_layout_t layout;
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t index;
    firmware_status_t status;

    if ((ReadU32(&buffer[0x00U]) != ACTIVE_RECORD_MAGIC) ||
        (ReadU16(&buffer[0x04U]) != RECORD_FORMAT_V1) ||
        (ReadU16(&buffer[0x06U]) != ACTIVE_RECORD_SIZE) ||
        (buffer[0x0CU] != ACTIVE_VALID_STATE) ||
        (ReadU16(&buffer[0x0EU]) != 0U) ||
        (ReadU16(&buffer[0x16U]) != 0U) ||
        (ReadU32(&buffer[ACTIVE_RECORD_MARKER]) != COMMIT_MARKER))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0x5CU; index < ACTIVE_RECORD_CRC_OFFSET; ++index)
    {
        if (buffer[index] != 0xFFU)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }

    status = CalculateCrc(
        service, buffer, ACTIVE_RECORD_CRC_OFFSET, &actual_crc);
    expected_crc = ReadU32(&buffer[ACTIVE_RECORD_CRC_OFFSET]);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (actual_crc != expected_crc)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    record->sequence = ReadU32(&buffer[0x08U]);
    record->active_pair = (boot_pair_t)buffer[0x0DU];
    status = SlotPolicy_GetPairLayout(record->active_pair, &layout);
    if (!FirmwareStatus_IsOk(status))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    record->release_version.major = ReadU16(&buffer[0x10U]);
    record->release_version.minor = ReadU16(&buffer[0x12U]);
    record->release_version.patch = ReadU16(&buffer[0x14U]);
    record->build_number = ReadU32(&buffer[0x18U]);
    record->app_size = ReadU32(&buffer[0x1CU]);
    record->app_crc32 = ReadU32(&buffer[0x20U]);
    record->gui_size = ReadU32(&buffer[0x24U]);
    record->gui_crc32 = ReadU32(&buffer[0x28U]);
    if (!FirmwareStatus_IsOk(
            SlotPolicy_ValidateImageSize(&layout.app, record->app_size)) ||
        !FirmwareStatus_IsOk(
            SlotPolicy_ValidateImageSize(&layout.gui, record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(
        record->package_id_hash,
        &buffer[0x2CU],
        BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(
        record->manifest_sha256,
        &buffer[0x3CU],
        BOOT_CONTROL_MANIFEST_HASH_SIZE);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ValidateRequestBuffer(
    boot_control_service_t *service,
    const uint8_t *buffer,
    boot_update_request_t *request)
{
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t index;
    firmware_status_t status;

    if ((ReadU32(&buffer[0x00U]) != REQUEST_RECORD_MAGIC) ||
        (ReadU16(&buffer[0x04U]) != RECORD_FORMAT_V1) ||
        (ReadU16(&buffer[0x06U]) != REQUEST_RECORD_SIZE) ||
        (buffer[0x0CU] > 1U) || (ReadU16(&buffer[0x0EU]) != 0U) ||
        (ReadU32(&buffer[REQUEST_RECORD_MARKER]) != COMMIT_MARKER))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (buffer[0x0DU] > (uint8_t)BOOT_UPDATE_REASON_PRODUCTION)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    for (index = 0x10U; index < REQUEST_RECORD_CRC_OFFSET; ++index)
    {
        if (buffer[index] != 0xFFU)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }

    status = CalculateCrc(
        service, buffer, REQUEST_RECORD_CRC_OFFSET, &actual_crc);
    expected_crc = ReadU32(&buffer[REQUEST_RECORD_CRC_OFFSET]);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (actual_crc != expected_crc)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    request->sequence = ReadU32(&buffer[0x08U]);
    request->requested = (buffer[0x0CU] != 0U);
    request->reason = (boot_update_reason_t)buffer[0x0DU];
    if ((request->requested == 0) && (request->reason != BOOT_UPDATE_REASON_NONE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadAndSelect(
    boot_control_service_t *service,
    record_kind_t kind,
    int *selected_slot,
    uint32_t *sequence)
{
    uint32_t address_a = (kind == RECORD_KIND_ACTIVE)
                             ? ACTIVE_RECORD_A_ADDRESS
                             : REQUEST_RECORD_A_ADDRESS;
    uint32_t address_b = (kind == RECORD_KIND_ACTIVE)
                             ? ACTIVE_RECORD_B_ADDRESS
                             : REQUEST_RECORD_B_ADDRESS;
    uint32_t size = (kind == RECORD_KIND_ACTIVE)
                        ? ACTIVE_RECORD_SIZE
                        : REQUEST_RECORD_SIZE;
    boot_active_record_t active_a;
    boot_active_record_t active_b;
    boot_update_request_t request_a;
    boot_update_request_t request_b;
    firmware_status_t status;
    firmware_status_t status_a;
    firmware_status_t status_b;
    int valid_a;
    int valid_b;
    uint32_t sequence_a;
    uint32_t sequence_b;

    *selected_slot = RECORD_SLOT_CONFLICT;
    *sequence = 0U;

    status = service->store->read(
        service->store->context, address_a, service->write_buffer, size);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = service->store->read(
        service->store->context, address_b, service->verify_buffer, size);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    if (kind == RECORD_KIND_ACTIVE)
    {
        status_a = ValidateActiveBuffer(
            service, service->write_buffer, &active_a);
        status_b = ValidateActiveBuffer(
            service, service->verify_buffer, &active_b);
        sequence_a = FirmwareStatus_IsOk(status_a) ? active_a.sequence : 0U;
        sequence_b = FirmwareStatus_IsOk(status_b) ? active_b.sequence : 0U;
    }
    else
    {
        status_a = ValidateRequestBuffer(
            service, service->write_buffer, &request_a);
        status_b = ValidateRequestBuffer(
            service, service->verify_buffer, &request_b);
        sequence_a = FirmwareStatus_IsOk(status_a) ? request_a.sequence : 0U;
        sequence_b = FirmwareStatus_IsOk(status_b) ? request_b.sequence : 0U;
    }

    if ((!FirmwareStatus_IsOk(status_a) &&
         (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)) ||
        (!FirmwareStatus_IsOk(status_b) &&
         (status_b != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_b != FIRMWARE_STATUS_OUT_OF_RANGE)))
    {
        return !FirmwareStatus_IsOk(status_a) &&
                       (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
                       (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)
                   ? status_a
                   : status_b;
    }
    valid_a = FirmwareStatus_IsOk(status_a);
    valid_b = FirmwareStatus_IsOk(status_b);

    if ((valid_a == 0) && (valid_b == 0))
    {
        *selected_slot = RECORD_SLOT_NONE;
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((valid_a != 0) && (valid_b == 0))
    {
        *selected_slot = 0;
        *sequence = sequence_a;
        return FIRMWARE_STATUS_OK;
    }
    if ((valid_a == 0) && (valid_b != 0))
    {
        *selected_slot = 1;
        *sequence = sequence_b;
        return FIRMWARE_STATUS_OK;
    }
    if (sequence_a == sequence_b)
    {
        if (memcmp(service->write_buffer, service->verify_buffer, size) != 0)
        {
            *selected_slot = RECORD_SLOT_CONFLICT;
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        *selected_slot = 0;
        *sequence = sequence_a;
        return FIRMWARE_STATUS_OK;
    }
    /* RFC 1982 serial arithmetic cannot order values exactly half a cycle apart. */
    if ((sequence_a - sequence_b) == 0x80000000UL)
    {
        *selected_slot = RECORD_SLOT_CONFLICT;
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (SequenceIsNewer(sequence_a, sequence_b))
    {
        *selected_slot = 0;
        *sequence = sequence_a;
    }
    else
    {
        *selected_slot = 1;
        *sequence = sequence_b;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t EncodeActive(
    boot_control_service_t *service,
    const boot_active_record_t *record,
    uint32_t sequence)
{
    boot_pair_layout_t layout;
    uint32_t crc;
    firmware_status_t status = SlotPolicy_GetPairLayout(
        record->active_pair, &layout);

    if (!FirmwareStatus_IsOk(status) ||
        !FirmwareStatus_IsOk(
            SlotPolicy_ValidateImageSize(&layout.app, record->app_size)) ||
        !FirmwareStatus_IsOk(
            SlotPolicy_ValidateImageSize(&layout.gui, record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memset(service->write_buffer, 0xFF, ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x00U], ACTIVE_RECORD_MAGIC);
    WriteU16(&service->write_buffer[0x04U], RECORD_FORMAT_V1);
    WriteU16(&service->write_buffer[0x06U], ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x08U], sequence);
    service->write_buffer[0x0CU] = ACTIVE_VALID_STATE;
    service->write_buffer[0x0DU] = (uint8_t)record->active_pair;
    WriteU16(&service->write_buffer[0x0EU], 0U);
    WriteU16(&service->write_buffer[0x10U], record->release_version.major);
    WriteU16(&service->write_buffer[0x12U], record->release_version.minor);
    WriteU16(&service->write_buffer[0x14U], record->release_version.patch);
    WriteU16(&service->write_buffer[0x16U], 0U);
    WriteU32(&service->write_buffer[0x18U], record->build_number);
    WriteU32(&service->write_buffer[0x1CU], record->app_size);
    WriteU32(&service->write_buffer[0x20U], record->app_crc32);
    WriteU32(&service->write_buffer[0x24U], record->gui_size);
    WriteU32(&service->write_buffer[0x28U], record->gui_crc32);
    memcpy(
        &service->write_buffer[0x2CU],
        record->package_id_hash,
        BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(
        &service->write_buffer[0x3CU],
        record->manifest_sha256,
        BOOT_CONTROL_MANIFEST_HASH_SIZE);
    status = CalculateCrc(
        service, service->write_buffer, ACTIVE_RECORD_CRC_OFFSET, &crc);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    WriteU32(&service->write_buffer[ACTIVE_RECORD_CRC_OFFSET], crc);
    WriteU32(&service->write_buffer[ACTIVE_RECORD_MARKER], INVALID_MARKER);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t EncodeRequest(
    boot_control_service_t *service,
    const boot_update_request_t *request,
    uint32_t sequence)
{
    uint32_t crc;
    firmware_status_t status;

    if (((request->requested == 0) &&
         (request->reason != BOOT_UPDATE_REASON_NONE)) ||
        ((request->requested != 0) &&
         ((request->reason <= BOOT_UPDATE_REASON_NONE) ||
          (request->reason > BOOT_UPDATE_REASON_PRODUCTION))))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    memset(service->write_buffer, 0xFF, REQUEST_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x00U], REQUEST_RECORD_MAGIC);
    WriteU16(&service->write_buffer[0x04U], RECORD_FORMAT_V1);
    WriteU16(&service->write_buffer[0x06U], REQUEST_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x08U], sequence);
    service->write_buffer[0x0CU] = (request->requested != 0) ? 1U : 0U;
    service->write_buffer[0x0DU] = (uint8_t)request->reason;
    WriteU16(&service->write_buffer[0x0EU], 0U);
    status = CalculateCrc(
        service, service->write_buffer, REQUEST_RECORD_CRC_OFFSET, &crc);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    WriteU32(&service->write_buffer[REQUEST_RECORD_CRC_OFFSET], crc);
    WriteU32(&service->write_buffer[REQUEST_RECORD_MARKER], INVALID_MARKER);
    return FIRMWARE_STATUS_OK;
}

static void Fail(boot_control_service_t *service, firmware_status_t status)
{
    service->state = SERVICE_RUN_STATE_FAILED;
    service->result.status = status;
    service->result.error = BOOT_ERROR_EEPROM_COMMIT;
    service->result.stage = (uint32_t)service->stage;
    service->result.native_error = (int32_t)status;
}

static firmware_status_t StartCommit(
    boot_control_service_t *service,
    record_kind_t kind,
    const void *record)
{
    int current_slot = RECORD_SLOT_CONFLICT;
    uint32_t current_sequence = 0U;
    firmware_status_t select_status;
    firmware_status_t status;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) ||
        (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    select_status = ReadAndSelect(
        service, kind, &current_slot, &current_sequence);
    if (!FirmwareStatus_IsOk(select_status) &&
        (current_slot != RECORD_SLOT_NONE))
    {
        return select_status;
    }

    if (kind == RECORD_KIND_ACTIVE)
    {
        service->record_size = ACTIVE_RECORD_SIZE;
        service->marker_offset = ACTIVE_RECORD_MARKER;
        service->target_address = (current_slot == 0)
                                      ? ACTIVE_RECORD_B_ADDRESS
                                      : ACTIVE_RECORD_A_ADDRESS;
        status = EncodeActive(
            service,
            (const boot_active_record_t *)record,
            current_sequence + 1U);
    }
    else
    {
        service->record_size = REQUEST_RECORD_SIZE;
        service->marker_offset = REQUEST_RECORD_MARKER;
        service->target_address = (current_slot == 0)
                                      ? REQUEST_RECORD_B_ADDRESS
                                      : REQUEST_RECORD_A_ADDRESS;
        status = EncodeRequest(
            service,
            (const boot_update_request_t *)record,
            current_sequence + 1U);
    }
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    service->write_offset = 0U;
    service->last_write_size = 0U;
    service->record_kind = (int)kind;
    service->stage = BOOT_CONTROL_STAGE_INVALIDATE_MARKER;
    service->state = SERVICE_RUN_STATE_RUNNING;
    service->result.status = FIRMWARE_STATUS_OK;
    service->result.error = BOOT_ERROR_NONE;
    service->result.stage = BOOT_CONTROL_STAGE_IDLE;
    service->result.native_error = 0;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_Init(
    boot_control_service_t *service,
    const boot_control_service_dependencies_t *dependencies)
{
    firmware_status_t status;

    if ((service == NULL) || (dependencies == NULL) ||
        (dependencies->store == NULL) ||
        (dependencies->store->get_info == NULL) ||
        (dependencies->store->read == NULL) ||
        (dependencies->store->write_page == NULL) ||
        (dependencies->store->is_ready == NULL) ||
        (dependencies->checksum == NULL) ||
        (dependencies->checksum->reset == NULL) ||
        (dependencies->checksum->update == NULL) ||
        (dependencies->checksum->get_value == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->store = dependencies->store;
    service->checksum = dependencies->checksum;
    status = service->store->get_info(
        service->store->context, &service->store_info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((service->store_info.capacity_bytes < 0x0280U) ||
        (service->store_info.page_size < sizeof(uint32_t)) ||
        (service->store_info.page_size > BOOT_CONTROL_MAX_RECORD_SIZE) ||
        !MarkerFitsPage(
            ACTIVE_RECORD_A_ADDRESS,
            ACTIVE_RECORD_MARKER,
            service->store_info.page_size) ||
        !MarkerFitsPage(
            ACTIVE_RECORD_B_ADDRESS,
            ACTIVE_RECORD_MARKER,
            service->store_info.page_size) ||
        !MarkerFitsPage(
            REQUEST_RECORD_A_ADDRESS,
            REQUEST_RECORD_MARKER,
            service->store_info.page_size) ||
        !MarkerFitsPage(
            REQUEST_RECORD_B_ADDRESS,
            REQUEST_RECORD_MARKER,
            service->store_info.page_size))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    service->state = SERVICE_RUN_STATE_IDLE;
    service->stage = BOOT_CONTROL_STAGE_IDLE;
    service->result.status = FIRMWARE_STATUS_OK;
    service->result.error = BOOT_ERROR_NONE;
    service->result.stage = BOOT_CONTROL_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_LoadActive(
    boot_control_service_t *service,
    boot_active_record_t *record)
{
    int selected_slot;
    uint32_t sequence;
    firmware_status_t status;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) ||
        (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = ReadAndSelect(
        service, RECORD_KIND_ACTIVE, &selected_slot, &sequence);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    (void)sequence;
    return ValidateActiveBuffer(
        service,
        (selected_slot == 0) ? service->write_buffer : service->verify_buffer,
        record);
}

firmware_status_t BootControlService_LoadUpdateRequest(
    boot_control_service_t *service,
    boot_update_request_t *request)
{
    int selected_slot;
    uint32_t sequence;
    firmware_status_t status;

    if ((service == NULL) || (request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) ||
        (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = ReadAndSelect(
        service, RECORD_KIND_REQUEST, &selected_slot, &sequence);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    (void)sequence;
    return ValidateRequestBuffer(
        service,
        (selected_slot == 0) ? service->write_buffer : service->verify_buffer,
        request);
}

firmware_status_t BootControlService_CommitActiveStart(
    boot_control_service_t *service,
    const boot_active_record_t *record)
{
    return StartCommit(service, RECORD_KIND_ACTIVE, record);
}

firmware_status_t BootControlService_CommitUpdateRequestStart(
    boot_control_service_t *service,
    const boot_update_request_t *request)
{
    return StartCommit(service, RECORD_KIND_REQUEST, request);
}

void BootControlService_Process(boot_control_service_t *service)
{
    firmware_status_t status;
    uint32_t page_remaining;
    uint32_t remaining;
    int ready;

    if ((service == NULL) ||
        (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case BOOT_CONTROL_STAGE_INVALIDATE_MARKER:
            status = service->store->write_page(
                service->store->context,
                service->target_address + service->marker_offset,
                &service->write_buffer[service->marker_offset],
                sizeof(uint32_t));
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WAIT_INVALIDATE;
            break;

        case BOOT_CONTROL_STAGE_WAIT_INVALIDATE:
        case BOOT_CONTROL_STAGE_WAIT_BODY:
        case BOOT_CONTROL_STAGE_WAIT_MARKER:
            status = service->store->is_ready(
                service->store->context, &ready);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            if (ready == 0)
            {
                break;
            }
            if (service->stage == BOOT_CONTROL_STAGE_WAIT_INVALIDATE)
            {
                service->stage = BOOT_CONTROL_STAGE_WRITE_BODY;
            }
            else if (service->stage == BOOT_CONTROL_STAGE_WAIT_BODY)
            {
                service->write_offset += service->last_write_size;
                service->stage =
                    (service->write_offset < service->marker_offset)
                        ? BOOT_CONTROL_STAGE_WRITE_BODY
                        : BOOT_CONTROL_STAGE_READ_BODY;
            }
            else
            {
                service->stage = BOOT_CONTROL_STAGE_VERIFY_FINAL;
            }
            break;

        case BOOT_CONTROL_STAGE_WRITE_BODY:
            page_remaining = service->store_info.page_size -
                             ((service->target_address + service->write_offset) %
                              service->store_info.page_size);
            remaining = service->marker_offset - service->write_offset;
            service->last_write_size =
                (remaining < page_remaining) ? remaining : page_remaining;
            status = service->store->write_page(
                service->store->context,
                service->target_address + service->write_offset,
                &service->write_buffer[service->write_offset],
                service->last_write_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WAIT_BODY;
            break;

        case BOOT_CONTROL_STAGE_READ_BODY:
            status = service->store->read(
                service->store->context,
                service->target_address,
                service->verify_buffer,
                service->marker_offset);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(
                     service->write_buffer,
                     service->verify_buffer,
                     service->marker_offset) != 0))
            {
                Fail(
                    service,
                    FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR
                                                : status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WRITE_MARKER;
            break;

        case BOOT_CONTROL_STAGE_WRITE_MARKER:
            WriteU32(
                &service->write_buffer[service->marker_offset], COMMIT_MARKER);
            status = service->store->write_page(
                service->store->context,
                service->target_address + service->marker_offset,
                &service->write_buffer[service->marker_offset],
                sizeof(uint32_t));
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WAIT_MARKER;
            break;

        case BOOT_CONTROL_STAGE_VERIFY_FINAL:
            status = service->store->read(
                service->store->context,
                service->target_address,
                service->verify_buffer,
                service->record_size);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(
                     service->write_buffer,
                     service->verify_buffer,
                     service->record_size) != 0))
            {
                Fail(
                    service,
                    FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR
                                                : status);
                break;
            }
            if (service->record_kind == (int)RECORD_KIND_ACTIVE)
            {
                boot_active_record_t active_record;

                status = ValidateActiveBuffer(
                    service, service->verify_buffer, &active_record);
            }
            else if (service->record_kind == (int)RECORD_KIND_REQUEST)
            {
                boot_update_request_t update_request;

                status = ValidateRequestBuffer(
                    service, service->verify_buffer, &update_request);
            }
            else
            {
                status = FIRMWARE_STATUS_INVALID_STATE;
            }
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->state = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status = FIRMWARE_STATUS_OK;
            service->result.error = BOOT_ERROR_NONE;
            service->result.stage = (uint32_t)service->stage;
            service->result.native_error = 0;
            service->stage = BOOT_CONTROL_STAGE_IDLE;
            break;

        default:
            Fail(service, FIRMWARE_STATUS_INVALID_STATE);
            break;
    }
}

service_run_state_t BootControlService_GetState(
    const boot_control_service_t *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *BootControlService_GetResult(
    const boot_control_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
