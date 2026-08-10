/**
 * @file boot_control_service.c
 * @brief 实现掉电安全的 Boot Control Active Record A/B 管理。
 *
 * 两个 EEPROM 槽位保存同一格式的 Active Record。提交时先让目标槽位 marker 无效，
 * 再分页写入并回读正文，最后单独写入提交 marker；任意掉电都只会保留旧记录或一份
 * 完整校验通过的新记录，不会把半写记录选为活动记录。
 */
#include "services/capability/boot_control_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/common/runtime_layout.h"

/** EEPROM 中 A 槽位的固定起始地址。 */
#define ACTIVE_RECORD_A_ADDRESS  0x0000U
/** EEPROM 中 B 槽位的固定起始地址，与 A 槽位保持一个完整记录间隔。 */
#define ACTIVE_RECORD_B_ADDRESS  0x0100U
/** Active Record V2/V3 的固定序列化字节数。 */
#define ACTIVE_RECORD_SIZE       BOOT_ACTIVE_RECORD_SIZE
/** CRC 覆盖区之后存放 CRC-32 的记录内偏移。 */
#define ACTIVE_RECORD_CRC_OFFSET 0x00F8U
/** 最后写入的 32 位提交 marker 的记录内偏移。 */
#define ACTIVE_RECORD_MARKER     0x00FCU

/** 标识 Active Record 格式的固定小端 magic 值。 */
#define ACTIVE_RECORD_MAGIC  0x52434248UL
/** 表示记录正文已完整写入并被原子提交的 marker。 */
#define COMMIT_MARKER        0x434F4D54UL
/** 提交前写入 marker 位置的无效值，确保半写记录不可被选择。 */
#define INVALID_MARKER       0xFFFFFFFFUL
/** 历史 V1 记录格式，仅用于明确拒绝而不进行兼容解释。 */
#define RECORD_FORMAT_V1     1U
/** 兼容读取的 Active Record V2 格式版本。 */
#define RECORD_FORMAT_V2     BOOT_ACTIVE_RECORD_FORMAT_V2
/** 保存 Therapy MCU 元数据的 Active Record V3。 */
#define RECORD_FORMAT_V3     BOOT_ACTIVE_RECORD_FORMAT_V3
/** V2 记录中表示已激活的 state 字节值。 */
#define ACTIVE_VALID_STATE   1U
/** A/B 均不可用时的内部槽位选择结果。 */
#define RECORD_SLOT_NONE     (-1)
/** A/B 同序列但字节不一致或序列不可排序时的内部冲突结果。 */
#define RECORD_SLOT_CONFLICT (-2)

/**
 * 将提交细阶段转换为稳定的诊断日志名称。
 *
 * @param stage 待描述的 Boot Control 提交阶段。
 * @return 指向只读阶段名称；未知枚举值返回 "unknown"。
 */
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

/**
 * 将内部槽位选择结果转换为诊断日志名称。
 *
 * @param slot A/B 槽位编号或内部的 NONE/CONFLICT 哨兵值。
 * @return 指向只读槽位名称；未知值返回 "unknown"。
 */
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

/**
 * 按小端序从未对齐字节缓冲区读取 16 位值。
 *
 * @param data 至少包含两个字节的源缓冲区。
 * @return 由 data[0..1] 解码得到的 uint16_t。
 */
static uint16_t ReadU16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}

/**
 * 按小端序从未对齐字节缓冲区读取 32 位值。
 *
 * @param data 至少包含四个字节的源缓冲区。
 * @return 由 data[0..3] 解码得到的 uint32_t。
 */
static uint32_t ReadU32(const uint8_t *data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

/**
 * 按小端序向未对齐字节缓冲区写入 16 位值。
 *
 * @param data 至少可写入两个字节的目标缓冲区。
 * @param value 待序列化的值。
 */
static void WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8U);
}

/**
 * 按小端序向未对齐字节缓冲区写入 32 位值。
 *
 * @param data 至少可写入四个字节的目标缓冲区。
 * @param value 待序列化的值。
 */
static void WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8U);
    data[2] = (uint8_t) (value >> 16U);
    data[3] = (uint8_t) (value >> 24U);
}

/**
 * 使用服务注入的 CRC 端口计算指定记录区域的校验值。
 *
 * 按 reset、update、get_value 串行调用；前一步失败时不继续访问底层 CRC 上下文。
 *
 * @param service 已初始化的 Boot Control 服务。
 * @param data 待计算 CRC 的连续字节。
 * @param size data 的字节数。
 * @param crc 成功时接收计算出的 CRC 值。
 * @return CRC 端口返回的最终状态。
 */
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

/**
 * 按 RFC 1982 风格的 32 位序列号算术判断候选值是否更新。
 *
 * 差值为零表示同一记录；差值小于半个序列空间表示 candidate 在 reference 之后。
 * 恰好相差半个空间的情况不能排序，由调用方作为冲突处理。
 *
 * @param candidate 待比较的候选序列号。
 * @param reference 参考序列号。
 * @return candidate 较新时返回非零，否则返回零。
 */
static int SequenceIsNewer(uint32_t candidate, uint32_t reference)
{
    uint32_t difference = candidate - reference;

    return (difference != 0U) && (difference < 0x80000000UL);
}

/**
 * 检查提交 marker 是否可由一次 EEPROM 页写完整覆盖。
 *
 * marker 不得跨页，否则最后一步的原子提交会退化为多次写操作，破坏掉电安全假设。
 *
 * @param record_address 记录在 EEPROM 中的起始地址。
 * @param marker_offset marker 在记录中的字节偏移。
 * @param page_size EEPROM 页写大小。
 * @return marker 完全位于单个页内时返回非零，否则返回零。
 */
static int MarkerFitsPage(uint32_t record_address, uint32_t marker_offset, uint32_t page_size)
{
    uint32_t page_offset = (record_address + marker_offset) % page_size;

    return page_offset <= (page_size - sizeof(uint32_t));
}

/**
 * 判断记录校验失败是否属于可通过另一 A/B 槽位恢复的状态。
 *
 * 存储 I/O 或 CRC 端口错误不能被当作“空槽位”忽略，否则可能在硬件故障时错误地
 * 选择另一份旧记录。
 *
 * @param status 单个记录解析或校验返回的状态。
 * @return 可视为槽位不可用时返回非零，否则返回零。
 */
static int IsRecordUnavailable(firmware_status_t status)
{
    return (status == FIRMWARE_STATUS_INVALID_STATE) ||
           (status == FIRMWARE_STATUS_OUT_OF_RANGE) ||
           (status == FIRMWARE_STATUS_NOT_SUPPORTED);
}

/**
 * 校验并反序列化一份 Active Record V2 缓冲区。
 *
 * 校验顺序为 magic、版本、长度、状态、保留字节、提交 marker、未使用填充值、CRC
 * 以及运行时 APP/GUI 分区大小。V1 的字段语义与 V2/V3 不兼容，明确返回不支持而不是
 * 冒险按 V2 解释。所有检查通过后才填充 record。
 *
 * @param service 已初始化的服务，用于 CRC 和运行时布局检查。
 * @param buffer 长度为 ACTIVE_RECORD_SIZE 的原始 EEPROM 记录。
 * @param record 成功时接收反序列化后的活动记录。
 * @return 成功时返回 FIRMWARE_STATUS_OK；格式、CRC 或布局不合法时返回错误。
 */
static firmware_status_t ValidateActiveBuffer(boot_control_service_t *service,
                                              const uint8_t *buffer, boot_active_record_t *record)
{
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t index;
    uint16_t format;
    firmware_status_t status;
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();

    if (ReadU32(&buffer[0x00U]) != ACTIVE_RECORD_MAGIC)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (ReadU16(&buffer[0x04U]) == RECORD_FORMAT_V1)
    {
        /* V1 的槽位身份和 CRC 字段语义不同，绝不可按 V2 重解释。 */
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    format = ReadU16(&buffer[0x04U]);
    if (((format != RECORD_FORMAT_V2) && (format != RECORD_FORMAT_V3)) ||
        (ReadU16(&buffer[0x06U]) != ACTIVE_RECORD_SIZE) || (buffer[0x0CU] != ACTIVE_VALID_STATE) ||
        (ReadU16(&buffer[0x0EU]) != 0U) || (ReadU16(&buffer[0x16U]) != 0U) ||
        (ReadU32(&buffer[ACTIVE_RECORD_MARKER]) != COMMIT_MARKER))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (format == RECORD_FORMAT_V2)
    {
        for (index = 0x94U; index < ACTIVE_RECORD_CRC_OFFSET; ++index)
        {
            /* V2 保留区域必须保持擦除态，防止未知字段绕过当前 schema。 */
            if (buffer[index] != 0xFFU)
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
        }
    }
    else
    {
        for (index = 0x95U; index < 0x98U; ++index)
        {
            if (buffer[index] != 0xFFU)
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
        }
        for (index = 0xC4U; index < ACTIVE_RECORD_CRC_OFFSET; ++index)
        {
            if (buffer[index] != 0xFFU)
            {
                return FIRMWARE_STATUS_INVALID_STATE;
            }
        }
        if ((buffer[0x94U] != UPDATE_COMPONENT_ALL) || (ReadU16(&buffer[0xA2U]) != 0U) ||
            (ReadU32(&buffer[0x98U]) == 0U) ||
            (ReadU32(&buffer[0x98U]) > BOOT_CONTROL_THERAPY_MAX_SIZE))
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

    memset(record, 0, sizeof(*record));
    record->format_version        = format;
    record->sequence              = ReadU32(&buffer[0x08U]);
    record->state                 = buffer[0x0CU];
    record->flags                 = buffer[0x0DU];
    record->release_version.major = ReadU16(&buffer[0x10U]);
    record->release_version.minor = ReadU16(&buffer[0x12U]);
    record->release_version.patch = ReadU16(&buffer[0x14U]);
    record->build_number          = ReadU32(&buffer[0x18U]);
    record->app_size              = ReadU32(&buffer[0x1CU]);
    record->gui_size              = ReadU32(&buffer[0x20U]);
    if ((record->app_size == 0U) || (record->app_size > layout->app_max_size) ||
        (record->gui_size == 0U) || (record->gui_size > layout->gui_max_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(record->package_id_hash, &buffer[0x24U], BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(record->manifest_sha256, &buffer[0x34U], BOOT_CONTROL_MANIFEST_HASH_SIZE);
    memcpy(record->app_sha256, &buffer[0x54U], BOOT_CONTROL_IMAGE_HASH_SIZE);
    memcpy(record->gui_sha256, &buffer[0x74U], BOOT_CONTROL_IMAGE_HASH_SIZE);
    record->component_mask = UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
    if (format == RECORD_FORMAT_V3)
    {
        record->component_mask          = buffer[0x94U];
        record->therapy_size            = ReadU32(&buffer[0x98U]);
        record->therapy_version.major   = ReadU16(&buffer[0x9CU]);
        record->therapy_version.minor   = ReadU16(&buffer[0x9EU]);
        record->therapy_version.patch   = ReadU16(&buffer[0xA0U]);
        memcpy(record->therapy_sha256, &buffer[0xA4U], BOOT_CONTROL_IMAGE_HASH_SIZE);
    }
    return FIRMWARE_STATUS_OK;
}

/**
 * 读取 A/B 槽位并选择最新且完整的 Active Record。
 *
 * 两个槽位都先独立校验。单槽位格式无效可回退到另一槽位；底层 I/O、CRC 等不可恢复
 * 错误直接向上传播。两份有效记录则按 RFC 1982 序列号选择；同序列必须逐字节相同，
 * 恰差半周也视为不可排序冲突。
 *
 * @param service 已初始化的服务。
 * @param selected_slot 成功时接收 0(A) 或 1(B)；失败时可接收内部哨兵值。
 * @param sequence 成功时接收被选择记录的序列号。
 * @return 成功时返回 FIRMWARE_STATUS_OK；无有效记录、冲突或底层错误时返回错误。
 */
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

    if ((!FirmwareStatus_IsOk(status_a) && !IsRecordUnavailable(status_a)) ||
        (!FirmwareStatus_IsOk(status_b) && !IsRecordUnavailable(status_b)))
    {
        /* 不把 I/O、CRC 等硬错误误判为空槽位，避免静默降级到旧记录。 */
        return !FirmwareStatus_IsOk(status_a) && !IsRecordUnavailable(status_a) ? status_a
                                                                                   : status_b;
    }
    valid_a = FirmwareStatus_IsOk(status_a);
    valid_b = FirmwareStatus_IsOk(status_b);

    if ((valid_a == 0) && (valid_b == 0))
    {
        *selected_slot = RECORD_SLOT_NONE;
        LOG_WARN("bootctl", "no valid active record found");
        return (status_a == FIRMWARE_STATUS_NOT_SUPPORTED) ||
                       (status_b == FIRMWARE_STATUS_NOT_SUPPORTED)
                   ? FIRMWARE_STATUS_NOT_SUPPORTED
                   : FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((valid_a != 0) && (valid_b == 0))
    {
        *selected_slot = 0;
        *sequence      = sequence_a;
        LOG_INFO("bootctl", "selected active record: slot=A sequence=%lu",
                 (unsigned long) sequence_a);
        return FIRMWARE_STATUS_OK;
    }
    if ((valid_a == 0) && (valid_b != 0))
    {
        *selected_slot = 1;
        *sequence      = sequence_b;
        LOG_INFO("bootctl", "selected active record: slot=B sequence=%lu",
                 (unsigned long) sequence_b);
        return FIRMWARE_STATUS_OK;
    }
    if (sequence_a == sequence_b)
    {
        /* 同序列的双副本必须完全一致，才能作为冗余而非数据冲突处理。 */
        if (memcmp(service->write_buffer, service->verify_buffer, ACTIVE_RECORD_SIZE) != 0)
        {
            *selected_slot = RECORD_SLOT_CONFLICT;
            LOG_ERROR("bootctl", "active record conflict: matching sequences differ");
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        *selected_slot = 0;
        *sequence      = sequence_a;
        LOG_INFO("bootctl", "selected duplicate active record: sequence=%lu",
                 (unsigned long) sequence_a);
        return FIRMWARE_STATUS_OK;
    }
    /* RFC 1982 序列算术无法比较恰好相差半个周期的两个值。 */
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
    LOG_INFO("bootctl", "selected active record: slot=%s sequence=%lu",
             RecordSlotName(*selected_slot), (unsigned long) *sequence);
    return FIRMWARE_STATUS_OK;
}

/**
 * 将内存中的 Active Record 编码为待提交的 V2/V3 EEPROM 镜像。
 *
 * 正文先填充为擦除态，再写入固定字段、摘要和 CRC。提交 marker 刻意保持无效；只有
 * Process 状态机在完成正文回读验证后才会写入有效 marker。
 *
 * @param service 已初始化的服务，write_buffer 将被完整覆盖。
 * @param record 调用方提供的待激活记录元数据。
 * @param sequence 本次提交分配的下一序列号。
 * @return 成功时返回 FIRMWARE_STATUS_OK；版本、状态、尺寸或 CRC 计算失败时返回错误。
 */
static firmware_status_t EncodeActive(boot_control_service_t *service,
                                      const boot_active_record_t *record, uint32_t sequence)
{
    uint32_t crc;
    uint16_t format;
    const boot_runtime_layout_t *layout = BootRuntimeLayout_Get();
    firmware_status_t status;

    format = record->format_version;
    if (format == 0U)
    {
        format = ((record->component_mask & UPDATE_COMPONENT_THERAPY) != 0U)
                     ? RECORD_FORMAT_V3
                     : RECORD_FORMAT_V2;
    }
    if ((format != RECORD_FORMAT_V2) && (format != RECORD_FORMAT_V3))
    {
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    }
    if ((record->state != 0U) && (record->state != ACTIVE_VALID_STATE))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((record->app_size == 0U) || (record->app_size > layout->app_max_size) ||
        (record->gui_size == 0U) || (record->gui_size > layout->gui_max_size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if ((format == RECORD_FORMAT_V3) &&
        ((record->component_mask != UPDATE_COMPONENT_ALL) || (record->therapy_size == 0U) ||
         (record->therapy_size > BOOT_CONTROL_THERAPY_MAX_SIZE)))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memset(service->write_buffer, 0xFF, ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x00U], ACTIVE_RECORD_MAGIC);
    WriteU16(&service->write_buffer[0x04U], format);
    WriteU16(&service->write_buffer[0x06U], ACTIVE_RECORD_SIZE);
    WriteU32(&service->write_buffer[0x08U], sequence);
    service->write_buffer[0x0CU] = ACTIVE_VALID_STATE;
    service->write_buffer[0x0DU] = record->flags;
    WriteU16(&service->write_buffer[0x0EU], 0U);
    WriteU16(&service->write_buffer[0x10U], record->release_version.major);
    WriteU16(&service->write_buffer[0x12U], record->release_version.minor);
    WriteU16(&service->write_buffer[0x14U], record->release_version.patch);
    WriteU16(&service->write_buffer[0x16U], 0U);
    WriteU32(&service->write_buffer[0x18U], record->build_number);
    WriteU32(&service->write_buffer[0x1CU], record->app_size);
    WriteU32(&service->write_buffer[0x20U], record->gui_size);
    memcpy(&service->write_buffer[0x24U], record->package_id_hash,
           BOOT_CONTROL_PACKAGE_ID_HASH_SIZE);
    memcpy(&service->write_buffer[0x34U], record->manifest_sha256, BOOT_CONTROL_MANIFEST_HASH_SIZE);
    memcpy(&service->write_buffer[0x54U], record->app_sha256, BOOT_CONTROL_IMAGE_HASH_SIZE);
    memcpy(&service->write_buffer[0x74U], record->gui_sha256, BOOT_CONTROL_IMAGE_HASH_SIZE);
    if (format == RECORD_FORMAT_V3)
    {
        service->write_buffer[0x94U] = (uint8_t)record->component_mask;
        WriteU32(&service->write_buffer[0x98U], record->therapy_size);
        WriteU16(&service->write_buffer[0x9CU], record->therapy_version.major);
        WriteU16(&service->write_buffer[0x9EU], record->therapy_version.minor);
        WriteU16(&service->write_buffer[0xA0U], record->therapy_version.patch);
        WriteU16(&service->write_buffer[0xA2U], 0U);
        memcpy(&service->write_buffer[0xA4U], record->therapy_sha256,
               BOOT_CONTROL_IMAGE_HASH_SIZE);
    }
    status = CalculateCrc(service, service->write_buffer, ACTIVE_RECORD_CRC_OFFSET, &crc);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    WriteU32(&service->write_buffer[ACTIVE_RECORD_CRC_OFFSET], crc);
    /* 目标槽位在最后 marker 写入前始终不可作为有效活动记录选择。 */
    WriteU32(&service->write_buffer[ACTIVE_RECORD_MARKER], INVALID_MARKER);
    return FIRMWARE_STATUS_OK;
}

/**
 * 记录不可恢复的提交失败并终止状态机。
 *
 * @param service 正在运行的服务。
 * @param status 触发失败的底层或校验状态。
 */
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

/**
 * 用已编码记录初始化一次异步 A/B 提交。
 *
 * @param service 已初始化且当前未运行的服务。
 * @param record 待写入的 Active Record 元数据。
 * @param target_address 目标 A 或 B 槽位的 EEPROM 起始地址。
 * @param next_sequence 本次提交应写入的序列号。
 * @return 成功时返回 FIRMWARE_STATUS_OK；编码或参数校验失败时返回错误。
 */
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
    LOG_INFO("bootctl", "commit begin: target=0x%08lx sequence=%lu",
             (unsigned long) target_address, (unsigned long) next_sequence);
    return FIRMWARE_STATUS_OK;
}

/**
 * 选择非当前活动的槽位，并启动新的 Active Record 提交。
 *
 * 若两个槽位均无有效记录，则从 A 槽位和序列号 1 开始；若已有有效记录，则写入其
 * 对侧槽位并将序列号递增。遇到格式不支持或 A/B 冲突时拒绝覆盖，以保留诊断证据。
 *
 * @param service 已初始化且当前未运行的服务。
 * @param record 待提交的活动记录。
 * @return 成功时返回 FIRMWARE_STATUS_OK；选择或编码失败时返回错误。
 */
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
    if (!FirmwareStatus_IsOk(select_status) &&
        ((current_slot != RECORD_SLOT_NONE) || (select_status == FIRMWARE_STATUS_NOT_SUPPORTED)))
    {
        return select_status;
    }

    LOG_INFO("bootctl", "commit target selected: current=%s next=%s", RecordSlotName(current_slot),
             (current_slot == 0) ? "B" : "A");
    return BeginCommit(service, record,
                       (current_slot == 0) ? ACTIVE_RECORD_B_ADDRESS : ACTIVE_RECORD_A_ADDRESS,
                       current_sequence + 1U);
}

/**
 * 校验 EEPROM/CRC 依赖、存储几何并初始化 Boot Control 服务。
 *
 * 初始化阶段要求两个固定记录槽位可容纳于 EEPROM，页大小能够单独写入 32 位 marker，
 * 且 marker 不跨页；这些条件是掉电原子提交协议的前提。
 *
 * @param service 由 Composition 静态创建的服务实例，必须尚未初始化。
 * @param dependencies 注入的 EEPROM 存储端口和 CRC 端口。
 * @return 成功时返回 FIRMWARE_STATUS_OK；依赖、设备信息或页写几何不匹配时返回错误。
 */
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
        /* 无法保证两个记录槽位和单页 marker 提交时，拒绝启用该服务。 */
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

/**
 * 加载当前最新且完整的 Active Record。
 *
 * 该 API 不可与异步提交并行调用；选槽后再次对已选缓冲区反序列化，确保输出记录只
 * 来自通过完整格式和 CRC 校验的槽位。
 *
 * @param service 已初始化且未运行提交状态机的服务。
 * @param record 成功时接收最新有效活动记录。
 * @return 成功时返回 FIRMWARE_STATUS_OK；无记录、冲突或存储错误时返回错误。
 */
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
        LOG_INFO("bootctl", "loaded active record: slot=%s sequence=%lu",
                 RecordSlotName(selected_slot), (unsigned long) record->sequence);
    }
    return status;
}

/**
 * 启动一次掉电安全的 Active Record 提交。
 *
 * 实际 EEPROM 写入由随后重复调用 BootControlService_Process 推进；本函数只选择
 * 目标槽位、分配序列号并准备待写镜像。
 *
 * @param service 已初始化且当前未运行的服务。
 * @param record 待提交的活动记录元数据。
 * @return 成功时返回 FIRMWARE_STATUS_OK；状态或记录不合法时返回错误。
 */
firmware_status_t BootControlService_CommitActiveStart(boot_control_service_t *service,
                                                       const boot_active_record_t *record)
{
    return StartCommit(service, record);
}

/**
 * 推进一次 Boot Control 增量提交状态机。
 *
 * 每次调用最多发起一次 EEPROM 操作或推进一个就绪状态：先使 marker 无效，随后按页
 * 写正文并回读比较，最后写入 marker，再回读完整记录验证。任何底层失败或比较失败
 * 都经 Fail 进入 FAILED；成功则进入 SUCCEEDED 并回到 IDLE 细阶段。
 *
 * @param service 已启动提交的服务；空指针或非 RUNNING 状态时无副作用。
 */
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
            /* 先破坏旧内容可能遗留的有效 marker，避免覆盖期间误选目标槽位。 */
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
                /* EEPROM 仍在内部写周期中，留待下次主循环继续轮询。 */
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
            /* 将正文切分为不跨 EEPROM 页边界的写操作。 */
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
            /* 有效 marker 前必须先确认全部正文已经按预期落盘。 */
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
            /* marker 是原子生效点，且初始化已检查其可由一次页写覆盖。 */
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
            /* 最终回读既比较字节镜像，也重做格式和 CRC 校验。 */
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

/**
 * 查询当前提交操作的粗粒度生命周期状态。
 *
 * @param service 服务实例。
 * @return service 为空时返回 SERVICE_RUN_STATE_FAILED，否则返回当前状态。
 */
service_run_state_t BootControlService_GetState(const boot_control_service_t *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED : service->state;
}

/**
 * 查询最近一次提交操作的详细结果。
 *
 * @param service 服务实例。
 * @return 指向服务内稳定结果对象的只读指针；service 为空时返回 NULL。
 */
const service_result_t *BootControlService_GetResult(const boot_control_service_t *service)
{
    return (service == NULL) ? NULL : &service->result;
}
