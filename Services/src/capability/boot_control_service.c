/**
 * @file boot_control_service.c
 * @brief Power-loss-safe Boot Control A/B record implementation.
 */
#include "services/capability/boot_control_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/capability/slot_policy.h"

#define ACTIVE_RECORD_A_ADDRESS  0x0000U
#define ACTIVE_RECORD_B_ADDRESS  0x0100U
#define ACTIVE_RECORD_SIZE       256U
#define ACTIVE_RECORD_CRC_OFFSET 0x00F8U
#define ACTIVE_RECORD_MARKER     0x00FCU

#define ACTIVE_RECORD_MAGIC  0x52434248UL
#define COMMIT_MARKER        0x434F4D54UL
#define INVALID_MARKER       0xFFFFFFFFUL
#define RECORD_FORMAT_V1     1U
#define ACTIVE_VALID_STATE   1U
#define RECORD_SLOT_NONE     (-1)
#define RECORD_SLOT_CONFLICT (-2)

static const char *BootPairName(boot_pair_t pair)
{
    switch (pair)
    {
        case BOOT_PAIR_NONE:
            return "none";
        case BOOT_PAIR_1:
            return "pair-1";
        case BOOT_PAIR_2:
            return "pair-2";
        default:
            return "unknown";
    }
}

static const char *BootControlStageName(boot_control_stage_t stage)
{
    switch (stage)
    {
        case BOOT_CONTROL_STAGE_IDLE:
            return "idle";
        case BOOT_CONTROL_STAGE_INVALIDATE_MARKER:
            return "invalidate-marker";
        case BOOT_CONTROL_STAGE_WAIT_INVALIDATE:
            return "wait-invalidate";
        case BOOT_CONTROL_STAGE_WRITE_BODY:
            return "write-body";
        case BOOT_CONTROL_STAGE_WAIT_BODY:
            return "wait-body";
        case BOOT_CONTROL_STAGE_READ_BODY:
            return "read-body";
        case BOOT_CONTROL_STAGE_WRITE_MARKER:
            return "write-marker";
        case BOOT_CONTROL_STAGE_WAIT_MARKER:
            return "wait-marker";
        case BOOT_CONTROL_STAGE_VERIFY_FINAL:
            return "verify-final";
        default:
            return "unknown";
    }
}

static const char *RecordSlotName(int slot)
{
    switch (slot)
    {
        case 0:
            return "A";
        case 1:
            return "B";
        case RECORD_SLOT_NONE:
            return "none";
        case RECORD_SLOT_CONFLICT:
            return "conflict";
        default:
            return "unknown";
    }
}

static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static void WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8U);
    data[2] = (uint8_t) (value >> 16U);
    data[3] = (uint8_t) (value >> 24U);
}

static firmware_status_t CalculateCrc(boot_control_service_t *service, const uint8_t *data,
                                      uint32_t size, uint32_t *crc)
{
    firmware_status_t status = service->checksum->reset(service->checksum->context);

    if (FirmwareStatus_IsOk(status))
    {
        status = service->checksum->update(service->checksum->context, data, size);
    }
    if (FirmwareStatus_IsOk(status))
    {
        status = service->checksum->get_value(service->checksum->context, crc);
    }
    return status;
}

static int SequenceIsNewer(uint32_t candidate, uint32_t reference)
{
    uint32_t difference = candidate - reference;

    return (difference != 0U) && (difference < 0x80000000UL);
}

static int MarkerFitsPage(uint32_t record_address, uint32_t marker_offset, uint32_t page_size)
{
    uint32_t page_offset = (record_address + marker_offset) % page_size;

    return page_offset <= (page_size - sizeof(uint32_t));
}

static firmware_status_t ValidateActiveBuffer(boot_control_service_t *service,
                                              const uint8_t *buffer, boot_active_record_t *record)
{
    boot_pair_layout_t layout;
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t index;
    firmware_status_t status;

    if ((ReadU32(&buffer[0x00U]) != ACTIVE_RECORD_MAGIC) ||
        (ReadU16(&buffer[0x04U]) != RECORD_FORMAT_V1) ||
        (ReadU16(&buffer[0x06U]) != ACTIVE_RECORD_SIZE) || (buffer[0x0CU] != ACTIVE_VALID_STATE) ||
        (ReadU16(&buffer[0x0EU]) != 0U) || (ReadU16(&buffer[0x16U]) != 0U) ||
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

    status       = CalculateCrc(service, buffer, ACTIVE_RECORD_CRC_OFFSET, &actual_crc);
    expected_crc = ReadU32(&buffer[ACTIVE_RECORD_CRC_OFFSET]);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if (actual_crc != expected_crc)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    record->sequence    = ReadU32(&buffer[0x08U]);
    record->active_pair = (boot_pair_t) buffer[0x0DU];
    status              = SlotPolicy_GetPairLayout(record->active_pair, &layout);
    if (!FirmwareStatus_IsOk(status))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    record->release_version.major = ReadU16(&buffer[0x10U]);
    record->release_version.minor = ReadU16(&buffer[0x12U]);
    record->release_version.patch = ReadU16(&buffer[0x14U]);
    record->build_number          = ReadU32(&buffer[0x18U]);
    record->app_size              = ReadU32(&buffer[0x1CU]);
    record->app_crc32             = ReadU32(&buffer[0x20U]);
    record->gui_size              = ReadU32(&buffer[0x24U]);
    record->gui_crc32             = ReadU32(&buffer[0x28U]);
    if (!FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.app, record->app_size)) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.gui, record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(record->package_id_hash, &buffer[0x2CU], BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(record->manifest_sha256, &buffer[0x3CU], BOOT_CONTROL_MANIFEST_HASH_SIZE);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadAndSelect(boot_control_service_t *service, int *selected_slot,
                                       uint32_t *sequence)
{
    boot_active_record_t active_a;
    boot_active_record_t active_b;
    firmware_status_t status;
    firmware_status_t status_a;
    firmware_status_t status_b;
    int valid_a;
    int valid_b;
    uint32_t sequence_a;
    uint32_t sequence_b;

    *selected_slot = RECORD_SLOT_CONFLICT;
    *sequence      = 0U;

    status = service->store->read(service->store->context, ACTIVE_RECORD_A_ADDRESS,
                                  service->write_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = service->store->read(service->store->context, ACTIVE_RECORD_B_ADDRESS,
                                  service->verify_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status_a   = ValidateActiveBuffer(service, service->write_buffer, &active_a);
    status_b   = ValidateActiveBuffer(service, service->verify_buffer, &active_b);
    sequence_a = FirmwareStatus_IsOk(status_a) ? active_a.sequence : 0U;
    sequence_b = FirmwareStatus_IsOk(status_b) ? active_b.sequence : 0U;

    if ((!FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)) ||
        (!FirmwareStatus_IsOk(status_b) && (status_b != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_b != FIRMWARE_STATUS_OUT_OF_RANGE)))
    {
        return !FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
                       (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)
                   ? status_a
                   : status_b;
    }
    valid_a = FirmwareStatus_IsOk(status_a);
    valid_b = FirmwareStatus_IsOk(status_b);

    if ((valid_a == 0) && (valid_b == 0))
    {
        *selected_slot = RECORD_SLOT_NONE;
        LOG_WARN("bootctl", "no valid active record found");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((valid_a != 0) && (valid_b == 0))
    {
        *selected_slot = 0;
        *sequence      = sequence_a;
        LOG_INFO("bootctl", "selected active record: slot=A sequence=%lu pair=%s",
                 (unsigned long) sequence_a, BootPairName(active_a.active_pair));
        return FIRMWARE_STATUS_OK;
    }
    if ((valid_a == 0) && (valid_b != 0))
    {
        *selected_slot = 1;
        *sequence      = sequence_b;
        LOG_INFO("bootctl", "selected active record: slot=B sequence=%lu pair=%s",
                 (unsigned long) sequence_b, BootPairName(active_b.active_pair));
        return FIRMWARE_STATUS_OK;
    }
    if (sequence_a == sequence_b)
    {
        if (memcmp(service->write_buffer, service->verify_buffer, ACTIVE_RECORD_SIZE) != 0)
        {
            *selected_slot = RECORD_SLOT_CONFLICT;
            LOG_ERROR("bootctl", "active record conflict: matching sequences differ");
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        *selected_slot = 0;
        *sequence      = sequence_a;
        LOG_INFO("bootctl", "selected duplicate active record: sequence=%lu pair=%s",
                 (unsigned long) sequence_a, BootPairName(active_a.active_pair));
        return FIRMWARE_STATUS_OK;
    }
    /* RFC 1982 serial arithmetic cannot order values exactly half a cycle apart. */
    if ((sequence_a - sequence_b) == 0x80000000UL)
    {
        *selected_slot = RECORD_SLOT_CONFLICT;
        LOG_ERROR("bootctl", "active record conflict: sequence half-cycle apart");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (SequenceIsNewer(sequence_a, sequence_b))
    {
        *selected_slot = 0;
        *sequence      = sequence_a;
    }
    else
    {
        *selected_slot = 1;
        *sequence      = sequence_b;
    }
    LOG_INFO("bootctl", "selected active record: slot=%s sequence=%lu pair=%s",
             RecordSlotName(*selected_slot), (unsigned long) *sequence,
             BootPairName((*selected_slot == 0) ? active_a.active_pair : active_b.active_pair));
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t EncodeActive(boot_control_service_t *service,
                                      const boot_active_record_t *record, uint32_t sequence)
{
    boot_pair_layout_t layout;
    uint32_t crc;
    firmware_status_t status = SlotPolicy_GetPairLayout(record->active_pair, &layout);

    if (!FirmwareStatus_IsOk(status) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.app, record->app_size)) ||
        !FirmwareStatus_IsOk(SlotPolicy_ValidateImageSize(&layout.gui, record->gui_size)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memset(service->write_buffer, 0xFF, ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x00U], ACTIVE_RECORD_MAGIC);
    WriteU16(&service->write_buffer[0x04U], RECORD_FORMAT_V1);
    WriteU16(&service->write_buffer[0x06U], ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x08U], sequence);
    service->write_buffer[0x0CU] = ACTIVE_VALID_STATE;
    service->write_buffer[0x0DU] = (uint8_t) record->active_pair;
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
    memcpy(&service->write_buffer[0x2CU], record->package_id_hash,
           BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(&service->write_buffer[0x3CU], record->manifest_sha256, BOOT_CONTROL_MANIFEST_HASH_SIZE);
    status = CalculateCrc(service, service->write_buffer, ACTIVE_RECORD_CRC_OFFSET, &crc);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    WriteU32(&service->write_buffer[ACTIVE_RECORD_CRC_OFFSET], crc);
    WriteU32(&service->write_buffer[ACTIVE_RECORD_MARKER], INVALID_MARKER);
    return FIRMWARE_STATUS_OK;
}

static void Fail(boot_control_service_t *service, firmware_status_t status)
{
    LOG_ERROR("bootctl", "commit failed: status=%d stage=%s address=0x%08lx", (int) status,
              BootControlStageName(service->stage), (unsigned long) service->target_address);
    service->state               = SERVICE_RUN_STATE_FAILED;
    service->result.status       = status;
    service->result.error        = BOOT_ERROR_EEPROM_COMMIT;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = (int32_t) status;
}

static firmware_status_t BeginCommit(boot_control_service_t *service,
                                     const boot_active_record_t *record, uint32_t target_address,
                                     uint32_t next_sequence)
{
    firmware_status_t status;

    service->record_size    = ACTIVE_RECORD_SIZE;
    service->marker_offset  = ACTIVE_RECORD_MARKER;
    service->target_address = target_address;
    status                  = EncodeActive(service, record, next_sequence);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    service->write_offset        = 0U;
    service->last_write_size     = 0U;
    service->stage               = BOOT_CONTROL_STAGE_INVALIDATE_MARKER;
    service->state               = SERVICE_RUN_STATE_RUNNING;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = BOOT_CONTROL_STAGE_IDLE;
    service->result.native_error = 0;
    LOG_INFO("bootctl", "commit begin: target=0x%08lx sequence=%lu pair=%s",
             (unsigned long) target_address, (unsigned long) next_sequence,
             BootPairName(record->active_pair));
    return FIRMWARE_STATUS_OK;
}

static int ActiveRecordsEqual(const boot_active_record_t *left, const boot_active_record_t *right)
{
    return (left->sequence == right->sequence) && (left->active_pair == right->active_pair) &&
           (left->release_version.major == right->release_version.major) &&
           (left->release_version.minor == right->release_version.minor) &&
           (left->release_version.patch == right->release_version.patch) &&
           (left->build_number == right->build_number) && (left->app_size == right->app_size) &&
           (left->app_crc32 == right->app_crc32) && (left->gui_size == right->gui_size) &&
           (left->gui_crc32 == right->gui_crc32) &&
           (memcmp(left->package_id_hash, right->package_id_hash, sizeof(left->package_id_hash)) ==
            0) &&
           (memcmp(left->manifest_sha256, right->manifest_sha256, sizeof(left->manifest_sha256)) ==
            0);
}

static firmware_status_t StartCommit(boot_control_service_t *service,
                                     const boot_active_record_t *record)
{
    int current_slot          = RECORD_SLOT_CONFLICT;
    uint32_t current_sequence = 0U;
    firmware_status_t select_status;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    select_status = ReadAndSelect(service, &current_slot, &current_sequence);
    if (!FirmwareStatus_IsOk(select_status) && (current_slot != RECORD_SLOT_NONE))
    {
        return select_status;
    }

    LOG_INFO("bootctl", "commit target selected: current=%s next=%s", RecordSlotName(current_slot),
             (current_slot == 0) ? "B" : "A");
    return BeginCommit(service, record,
                       (current_slot == 0) ? ACTIVE_RECORD_B_ADDRESS : ACTIVE_RECORD_A_ADDRESS,
                       current_sequence + 1U);
}

firmware_status_t BootControlService_Init(boot_control_service_t *service,
                                          const boot_control_service_dependencies_t *dependencies)
{
    firmware_status_t status;

    if ((service == NULL) || (dependencies == NULL) || (dependencies->store == NULL) ||
        (dependencies->store->get_info == NULL) || (dependencies->store->read == NULL) ||
        (dependencies->store->write_page == NULL) || (dependencies->store->is_ready == NULL) ||
        (dependencies->checksum == NULL) || (dependencies->checksum->reset == NULL) ||
        (dependencies->checksum->update == NULL) || (dependencies->checksum->get_value == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    service->store    = dependencies->store;
    service->checksum = dependencies->checksum;
    status            = service->store->get_info(service->store->context, &service->store_info);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    if ((service->store_info.capacity_bytes < 0x0200U) ||
        (service->store_info.page_size < sizeof(uint32_t)) ||
        (service->store_info.page_size > BOOT_CONTROL_MAX_RECORD_SIZE) ||
        !MarkerFitsPage(ACTIVE_RECORD_A_ADDRESS, ACTIVE_RECORD_MARKER,
                        service->store_info.page_size) ||
        !MarkerFitsPage(ACTIVE_RECORD_B_ADDRESS, ACTIVE_RECORD_MARKER,
                        service->store_info.page_size))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }

    service->state               = SERVICE_RUN_STATE_IDLE;
    service->stage               = BOOT_CONTROL_STAGE_IDLE;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = BOOT_CONTROL_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_LoadActive(boot_control_service_t *service,
                                                boot_active_record_t *record)
{
    int selected_slot;
    uint32_t sequence;
    firmware_status_t status;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = ReadAndSelect(service, &selected_slot, &sequence);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    (void) sequence;
    status = ValidateActiveBuffer(
        service, (selected_slot == 0) ? service->write_buffer : service->verify_buffer, record);
    if (FirmwareStatus_IsOk(status))
    {
        LOG_INFO("bootctl", "loaded active record: slot=%s pair=%s sequence=%lu",
                 RecordSlotName(selected_slot), BootPairName(record->active_pair),
                 (unsigned long) record->sequence);
    }
    return status;
}

firmware_status_t BootControlService_LoadPairCandidate(boot_control_service_t *service,
                                                       boot_pair_t pair,
                                                       boot_active_record_t *record)
{
    boot_active_record_t active_a;
    boot_active_record_t active_b;
    firmware_status_t status;
    firmware_status_t status_a;
    firmware_status_t status_b;
    int valid_a;
    int valid_b;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((pair != BOOT_PAIR_1) && (pair != BOOT_PAIR_2))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = service->store->read(service->store->context, ACTIVE_RECORD_A_ADDRESS,
                                  service->write_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = service->store->read(service->store->context, ACTIVE_RECORD_B_ADDRESS,
                                  service->verify_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status_a = ValidateActiveBuffer(service, service->write_buffer, &active_a);
    status_b = ValidateActiveBuffer(service, service->verify_buffer, &active_b);
    if ((!FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)) ||
        (!FirmwareStatus_IsOk(status_b) && (status_b != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_b != FIRMWARE_STATUS_OUT_OF_RANGE)))
    {
        return !FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
                       (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)
                   ? status_a
                   : status_b;
    }

    valid_a = FirmwareStatus_IsOk(status_a) && (active_a.active_pair == pair);
    valid_b = FirmwareStatus_IsOk(status_b) && (active_b.active_pair == pair);
    if ((valid_a == 0) && (valid_b == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((valid_a != 0) && (valid_b == 0))
    {
        *record = active_a;
        LOG_INFO("bootctl", "loaded pair candidate: pair=%s slot=A sequence=%lu",
                 BootPairName(pair), (unsigned long) active_a.sequence);
        return FIRMWARE_STATUS_OK;
    }
    if ((valid_a == 0) && (valid_b != 0))
    {
        *record = active_b;
        LOG_INFO("bootctl", "loaded pair candidate: pair=%s slot=B sequence=%lu",
                 BootPairName(pair), (unsigned long) active_b.sequence);
        return FIRMWARE_STATUS_OK;
    }
    if (active_a.sequence == active_b.sequence)
    {
        if (memcmp(service->write_buffer, service->verify_buffer, ACTIVE_RECORD_SIZE) != 0)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        *record = active_a;
        LOG_INFO("bootctl", "loaded duplicate pair candidate: pair=%s sequence=%lu",
                 BootPairName(pair), (unsigned long) active_a.sequence);
        return FIRMWARE_STATUS_OK;
    }
    if ((active_a.sequence - active_b.sequence) == 0x80000000UL)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    *record = SequenceIsNewer(active_a.sequence, active_b.sequence) ? active_a : active_b;
    LOG_INFO("bootctl", "loaded pair candidate: pair=%s sequence=%lu", BootPairName(pair),
             (unsigned long) record->sequence);
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BootControlService_CommitActiveStart(boot_control_service_t *service,
                                                       const boot_active_record_t *record)
{
    return StartCommit(service, record);
}

firmware_status_t BootControlService_CommitRecoveredStart(boot_control_service_t *service,
                                                          const boot_active_record_t *record)
{
    boot_active_record_t active_a;
    boot_active_record_t active_b;
    firmware_status_t status;
    firmware_status_t status_a;
    firmware_status_t status_b;
    int matches_a;
    int matches_b;

    if ((service == NULL) || (record == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((service->initialized == 0) || (service->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = service->store->read(service->store->context, ACTIVE_RECORD_A_ADDRESS,
                                  service->write_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = service->store->read(service->store->context, ACTIVE_RECORD_B_ADDRESS,
                                  service->verify_buffer, ACTIVE_RECORD_SIZE);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status_a = ValidateActiveBuffer(service, service->write_buffer, &active_a);
    status_b = ValidateActiveBuffer(service, service->verify_buffer, &active_b);
    if ((!FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)) ||
        (!FirmwareStatus_IsOk(status_b) && (status_b != FIRMWARE_STATUS_INVALID_STATE) &&
         (status_b != FIRMWARE_STATUS_OUT_OF_RANGE)))
    {
        return !FirmwareStatus_IsOk(status_a) && (status_a != FIRMWARE_STATUS_INVALID_STATE) &&
                       (status_a != FIRMWARE_STATUS_OUT_OF_RANGE)
                   ? status_a
                   : status_b;
    }

    matches_a = FirmwareStatus_IsOk(status_a) && ActiveRecordsEqual(&active_a, record);
    matches_b = FirmwareStatus_IsOk(status_b) && ActiveRecordsEqual(&active_b, record);
    if ((matches_a == 0) && (matches_b == 0))
    {
        LOG_ERROR("bootctl", "recovered commit source record not found");
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* Preserve one exact, already-validated source record throughout the
     * recovery commit. If both copies match, preserve A. */
    LOG_INFO("bootctl", "recovered commit target selected: source=%s next=%s",
             (matches_a != 0) ? "A" : "B", (matches_a != 0) ? "B" : "A");
    return BeginCommit(service, record,
                       (matches_a != 0) ? ACTIVE_RECORD_B_ADDRESS : ACTIVE_RECORD_A_ADDRESS,
                       record->sequence + 1U);
}

void BootControlService_Process(boot_control_service_t *service)
{
    firmware_status_t status;
    uint32_t page_remaining;
    uint32_t remaining;
    int ready;

    if ((service == NULL) || (service->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }

    switch (service->stage)
    {
        case BOOT_CONTROL_STAGE_INVALIDATE_MARKER:
            LOG_DEBUG("bootctl", "invalidate marker: address=0x%08lx",
                      (unsigned long) (service->target_address + service->marker_offset));
            status = service->store->write_page(
                service->store->context, service->target_address + service->marker_offset,
                &service->write_buffer[service->marker_offset], sizeof(uint32_t));
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
            status = service->store->is_ready(service->store->context, &ready);
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
                LOG_DEBUG("bootctl", "marker invalidated");
                service->stage = BOOT_CONTROL_STAGE_WRITE_BODY;
            }
            else if (service->stage == BOOT_CONTROL_STAGE_WAIT_BODY)
            {
                service->write_offset += service->last_write_size;
                LOG_DEBUG("bootctl", "body write complete: offset=%lu/%lu",
                          (unsigned long) service->write_offset,
                          (unsigned long) service->marker_offset);
                service->stage = (service->write_offset < service->marker_offset)
                                     ? BOOT_CONTROL_STAGE_WRITE_BODY
                                     : BOOT_CONTROL_STAGE_READ_BODY;
            }
            else
            {
                LOG_DEBUG("bootctl", "commit marker written");
                service->stage = BOOT_CONTROL_STAGE_VERIFY_FINAL;
            }
            break;

        case BOOT_CONTROL_STAGE_WRITE_BODY:
            page_remaining =
                service->store_info.page_size -
                ((service->target_address + service->write_offset) % service->store_info.page_size);
            remaining                = service->marker_offset - service->write_offset;
            service->last_write_size = (remaining < page_remaining) ? remaining : page_remaining;
            LOG_DEBUG("bootctl", "write body: address=0x%08lx size=%lu",
                      (unsigned long) (service->target_address + service->write_offset),
                      (unsigned long) service->last_write_size);
            status = service->store->write_page(
                service->store->context, service->target_address + service->write_offset,
                &service->write_buffer[service->write_offset], service->last_write_size);
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WAIT_BODY;
            break;

        case BOOT_CONTROL_STAGE_READ_BODY:
            LOG_DEBUG("bootctl", "verify body before marker");
            status = service->store->read(service->store->context, service->target_address,
                                          service->verify_buffer, service->marker_offset);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(service->write_buffer, service->verify_buffer, service->marker_offset) !=
                 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WRITE_MARKER;
            break;

        case BOOT_CONTROL_STAGE_WRITE_MARKER:
            WriteU32(&service->write_buffer[service->marker_offset], COMMIT_MARKER);
            LOG_DEBUG("bootctl", "write commit marker: address=0x%08lx",
                      (unsigned long) (service->target_address + service->marker_offset));
            status = service->store->write_page(
                service->store->context, service->target_address + service->marker_offset,
                &service->write_buffer[service->marker_offset], sizeof(uint32_t));
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->stage = BOOT_CONTROL_STAGE_WAIT_MARKER;
            break;

        case BOOT_CONTROL_STAGE_VERIFY_FINAL:
            LOG_DEBUG("bootctl", "verify final record");
            status = service->store->read(service->store->context, service->target_address,
                                          service->verify_buffer, service->record_size);
            if (!FirmwareStatus_IsOk(status) ||
                (memcmp(service->write_buffer, service->verify_buffer, service->record_size) != 0))
            {
                Fail(service, FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_IO_ERROR : status);
                break;
            }
            {
                boot_active_record_t active_record;

                status = ValidateActiveBuffer(service, service->verify_buffer, &active_record);
            }
            if (!FirmwareStatus_IsOk(status))
            {
                Fail(service, status);
                break;
            }
            service->state               = SERVICE_RUN_STATE_SUCCEEDED;
            service->result.status       = FIRMWARE_STATUS_OK;
            service->result.error        = BOOT_ERROR_NONE;
            service->result.stage        = (uint32_t) service->stage;
            service->result.native_error = 0;
            service->stage               = BOOT_CONTROL_STAGE_IDLE;
            LOG_INFO("bootctl", "commit succeeded: address=0x%08lx",
                     (unsigned long) service->target_address);
            break;

        default:
            Fail(service, FIRMWARE_STATUS_INVALID_STATE);
            break;
    }
}

service_run_state_t BootControlService_GetState(const boot_control_service_t *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

const service_result_t *BootControlService_GetResult(const boot_control_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
