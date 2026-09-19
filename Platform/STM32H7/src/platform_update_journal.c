#include "platform/platform_update_journal.h"

#include <stddef.h>

#include "at24.h"
#include "bsp/bsp_at24_bus.h"
#include "platform/platform_system.h"

#define PLATFORM_UPDATE_JOURNAL_EEPROM_ADDRESS 0x50U
#define PLATFORM_UPDATE_JOURNAL_EEPROM_OFFSET  64U
#define PLATFORM_UPDATE_JOURNAL_WRITE_DELAY_MS 5U
#define PLATFORM_UPDATE_JOURNAL_PROBE_RETRIES  8U
#define PLATFORM_UPDATE_JOURNAL_MAGIC          0x55524A4CUL

typedef struct
{
    uint32_t magic;
    platform_update_journal_t journal;
} journal_storage_t;

_Static_assert(sizeof(journal_storage_t) <= 64U, "journal must fit in one AT24 page");

static uint32_t Crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;
    for (i = 0U; i < size; ++i)
    {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static at24_t s_eeprom;
static uint8_t s_initialized;

static at24_status_t EepromRead(void *context, uint8_t address, uint16_t offset,
                                uint8_t *data, uint32_t size)
{
    (void) context;
    return BspAt24Bus_Read(address, offset, data, size);
}

static at24_status_t EepromWrite(void *context, uint8_t address, uint16_t offset,
                                 const uint8_t *data, uint32_t size)
{
    (void) context;
    return BspAt24Bus_Write(address, offset, data, size);
}

static at24_status_t EepromProbe(void *context, uint8_t address)
{
    (void) context;
    return BspAt24Bus_Probe(address);
}

static uint32_t NowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

static firmware_status_t EnsureInitialized(void)
{
    const at24_port_t port = {.context = NULL,
                              .read = EepromRead,
                              .write = EepromWrite,
                              .probe_ready = EepromProbe,
                              .now_ms = NowMs,
                              .set_write_enabled = NULL};
    const at24_config_t config = {.device_address_7bit = PLATFORM_UPDATE_JOURNAL_EEPROM_ADDRESS,
                                  .write_timeout_ms = 50U};
    at24_status_t status;
    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }
    status = At24_Init(&s_eeprom, &port, &config);
    if (status != AT24_STATUS_OK)
    {
        return status;
    }
    status = At24_Probe(&s_eeprom);
    if (status != AT24_STATUS_OK)
    {
        return status;
    }
    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadStorage(journal_storage_t *storage)
{
    if (storage == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    {
        firmware_status_t status = EnsureInitialized();
        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
    }
    return At24_Read(&s_eeprom, PLATFORM_UPDATE_JOURNAL_EEPROM_OFFSET,
                     storage, (uint32_t) sizeof(*storage));
}

static firmware_status_t WriteStorage(const journal_storage_t *storage)
{
    firmware_status_t status;
    uint32_t retry;
    status = EnsureInitialized();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    status = At24_WritePageStart(&s_eeprom, PLATFORM_UPDATE_JOURNAL_EEPROM_OFFSET,
                                 storage, (uint32_t) sizeof(*storage));
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    PlatformSystem_DelayMs(PLATFORM_UPDATE_JOURNAL_WRITE_DELAY_MS);
    for (retry = 0U; retry < PLATFORM_UPDATE_JOURNAL_PROBE_RETRIES; ++retry)
    {
        status = At24_OperationPoll(&s_eeprom);
        if (status == FIRMWARE_STATUS_OK)
        {
            at24_operation_result_t result;
            status = At24_GetOperationResult(&s_eeprom, &result);
            if ((status == FIRMWARE_STATUS_OK) &&
                (result.state == AT24_OPERATION_SUCCEEDED))
            {
                return FIRMWARE_STATUS_OK;
            }
            if ((status == FIRMWARE_STATUS_OK) &&
                (result.state == AT24_OPERATION_FAILED))
            {
                return result.status;
            }
        }
        PlatformSystem_DelayMs(1U);
    }
    return status;
}

firmware_status_t PlatformUpdateJournal_Read(platform_update_journal_t *journal)
{
    journal_storage_t storage;
    firmware_status_t status;
    if (journal == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = ReadStorage(&storage);
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    if ((storage.magic != PLATFORM_UPDATE_JOURNAL_MAGIC) ||
        (storage.journal.state > UPDATE_JOURNAL_COMMITTING) ||
        (storage.journal.crc != Crc32(&storage.journal,
                                      offsetof(platform_update_journal_t, crc))))
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    *journal = storage.journal;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformUpdateJournal_Write(const platform_update_journal_t *journal)
{
    journal_storage_t storage;
    if ((journal == NULL) || (journal->state > UPDATE_JOURNAL_COMMITTING))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    storage.magic = PLATFORM_UPDATE_JOURNAL_MAGIC;
    storage.journal = *journal;
    storage.journal.crc = Crc32(&storage.journal,
                                offsetof(platform_update_journal_t, crc));
    return WriteStorage(&storage);
}

firmware_status_t PlatformUpdateJournal_Clear(void)
{
    const journal_storage_t storage = {0};
    return WriteStorage(&storage);
}
