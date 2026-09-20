#include "platform/platform_nv_storage.h"

#include <stddef.h>

#include "at24.h"
#include "bsp/bsp_at24_bus.h"
#include "platform/platform_system.h"

#define PLATFORM_NV_STORAGE_I2C_ADDRESS      0x50U
#define PLATFORM_NV_STORAGE_WRITE_TIMEOUT_MS 50U

static at24_t s_eeprom;
static uint8_t s_initialized;

static at24_status_t EepromRead(void *context, uint8_t address, uint16_t offset, uint8_t *data,
                                uint32_t size)
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

firmware_status_t PlatformNvStorage_Init(void)
{
    const at24_port_t port     = {.context           = NULL,
                                  .read              = EepromRead,
                                  .write             = EepromWrite,
                                  .probe_ready       = EepromProbe,
                                  .now_ms            = NowMs,
                                  .set_write_enabled = NULL};
    const at24_config_t config = {.device_address_7bit = PLATFORM_NV_STORAGE_I2C_ADDRESS,
                                  .write_timeout_ms    = PLATFORM_NV_STORAGE_WRITE_TIMEOUT_MS};
    at24_status_t status;

    if (s_initialized != 0U)
    {
        return At24_Probe(&s_eeprom);
    }
    status = At24_Init(&s_eeprom, &port, &config);
    if (status != AT24_STATUS_OK)
    {
        return status;
    }
    s_initialized = 1U;
    status        = At24_Probe(&s_eeprom);
    if (status != AT24_STATUS_OK)
    {
        return status;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformNvStorage_Read(uint32_t offset, void *data, size_t size)
{
    firmware_status_t status;
    if ((data == NULL) || (size == 0U) || (size > UINT32_MAX))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = PlatformNvStorage_Init();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    return At24_Read(&s_eeprom, offset, data, (uint32_t) size);
}

firmware_status_t PlatformNvStorage_Write(uint32_t offset, const void *data, size_t size)
{
    const uint8_t *source = (const uint8_t *) data;
    uint32_t remaining    = (uint32_t) size;
    firmware_status_t status;

    if ((data == NULL) || (size == 0U) || (size > UINT32_MAX))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    status = PlatformNvStorage_Init();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }
    while (remaining != 0U)
    {
        uint32_t page_remaining = AT24C128_PAGE_SIZE_BYTES - (offset % AT24C128_PAGE_SIZE_BYTES);
        uint32_t chunk          = (remaining < page_remaining) ? remaining : page_remaining;
        at24_operation_result_t result;

        status = At24_WritePageStart(&s_eeprom, offset, source, chunk);
        if (status != AT24_STATUS_OK)
        {
            return status;
        }
        for (;;)
        {
            status = At24_OperationPoll(&s_eeprom);
            if (status != AT24_STATUS_OK && status != FIRMWARE_STATUS_NOT_FOUND)
            {
                return status;
            }
            status = At24_GetOperationResult(&s_eeprom, &result);
            if (status != AT24_STATUS_OK)
            {
                return status;
            }
            if (result.state == AT24_OPERATION_SUCCEEDED)
            {
                break;
            }
            if (result.state == AT24_OPERATION_FAILED)
            {
                return result.status;
            }
            PlatformSystem_WatchdogRefresh();
            PlatformSystem_DelayMs(1U);
        }
        offset += chunk;
        source += chunk;
        remaining -= chunk;
    }
    return FIRMWARE_STATUS_OK;
}
