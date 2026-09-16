#include "platform/platform_boot_control.h"

#include <stddef.h>

#include "at24.h"
#include "bsp/bsp_i2c.h"
#include "platform/platform_system.h"

#define PLATFORM_BOOT_CONTROL_EEPROM_OFFSET 0U
#define PLATFORM_BOOT_CONTROL_I2C_ADDRESS   0x50U
#define PLATFORM_BOOT_CONTROL_WRITE_TIMEOUT 20U

static at24_t s_eeprom;
static uint8_t s_initialized;

_Static_assert(sizeof(platform_boot_control_t) <= AT24C128_PAGE_SIZE_BYTES,
               "BootControl must fit in one AT24 page");

static at24_status_t MapFirmwareToAt24(firmware_status_t status)
{
    switch (status)
    {
        case FIRMWARE_STATUS_OK:
            return AT24_STATUS_OK;
        case FIRMWARE_STATUS_INVALID_ARGUMENT:
            return AT24_STATUS_INVALID_ARGUMENT;
        case FIRMWARE_STATUS_INVALID_STATE:
            return AT24_STATUS_INVALID_STATE;
        case FIRMWARE_STATUS_TIMEOUT:
            return AT24_STATUS_TIMEOUT;
        default:
            return AT24_STATUS_IO_ERROR;
    }
}

static firmware_status_t MapAt24Status(at24_status_t status)
{
    switch (status)
    {
        case AT24_STATUS_OK:
            return FIRMWARE_STATUS_OK;
        case AT24_STATUS_INVALID_ARGUMENT:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        case AT24_STATUS_INVALID_STATE:
            return FIRMWARE_STATUS_INVALID_STATE;
        case AT24_STATUS_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;
        case AT24_STATUS_IO_ERROR:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static at24_status_t EepromRead(void *context, uint8_t device_address_7bit, uint16_t memory_address,
                                uint8_t *data, uint32_t size)
{
    (void) context;
    return MapFirmwareToAt24(BspI2c_ReadMemory(device_address_7bit, memory_address, data, size));
}

static at24_status_t EepromWrite(void *context, uint8_t device_address_7bit,
                                 uint16_t memory_address, const uint8_t *data, uint32_t size)
{
    (void) context;
    return MapFirmwareToAt24(BspI2c_WriteMemory(device_address_7bit, memory_address, data, size));
}

static at24_status_t EepromProbe(void *context, uint8_t device_address_7bit, int *ready)
{
    (void) context;
    return MapFirmwareToAt24(BspI2c_Probe(device_address_7bit, ready));
}

static uint32_t NowMs(void *context)
{
    (void) context;
    return PlatformSystem_GetMs();
}

static uint32_t CalculateCrc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc         = 0xFFFFFFFFUL;
    size_t i;

    for (i = 0U; i < size; ++i)
    {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc           = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static firmware_status_t WaitForWrite(void)
{
    at24_operation_result_t result;
    at24_status_t status;

    for (;;)
    {
        status = At24_OperationPoll(&s_eeprom);
        if (status != AT24_STATUS_OK)
        {
            return MapAt24Status(status);
        }

        status = At24_GetOperationResult(&s_eeprom, &result);
        if (status != AT24_STATUS_OK)
        {
            return MapAt24Status(status);
        }
        if (result.state == AT24_OPERATION_SUCCEEDED)
        {
            return FIRMWARE_STATUS_OK;
        }
        if (result.state == AT24_OPERATION_FAILED)
        {
            return MapAt24Status(result.status);
        }
        if (result.state != AT24_OPERATION_BUSY)
        {
            return FIRMWARE_STATUS_INVALID_STATE;
        }

        PlatformSystem_WatchdogRefresh();
        PlatformSystem_DelayMs(1U);
    }
}

firmware_status_t PlatformBootControl_Init(void)
{
    const at24_port_t port     = {.context           = NULL,
                                  .read              = EepromRead,
                                  .write             = EepromWrite,
                                  .probe_ready       = EepromProbe,
                                  .now_ms            = NowMs,
                                  .set_write_enabled = NULL};
    const at24_config_t config = {.device_address_7bit = PLATFORM_BOOT_CONTROL_I2C_ADDRESS,
                                  .write_timeout_ms    = PLATFORM_BOOT_CONTROL_WRITE_TIMEOUT};
    firmware_status_t status;
    at24_status_t driver_status;

    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    status = BspI2c_Init();
    if (status != FIRMWARE_STATUS_OK)
    {
        return status;
    }

    driver_status = At24_Init(&s_eeprom, &port, &config);
    if (driver_status != AT24_STATUS_OK)
    {
        return MapAt24Status(driver_status);
    }
    driver_status = At24_Probe(&s_eeprom);
    if (driver_status != AT24_STATUS_OK)
    {
        return MapAt24Status(driver_status);
    }

    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformBootControl_Read(platform_boot_control_t *control)
{
    at24_status_t status;
    uint32_t expected_crc;

    if (control == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = At24_Read(&s_eeprom, PLATFORM_BOOT_CONTROL_EEPROM_OFFSET, control,
                       (uint32_t) sizeof(*control));
    if (status != AT24_STATUS_OK)
    {
        return MapAt24Status(status);
    }

    expected_crc = CalculateCrc32(control, offsetof(platform_boot_control_t, crc32));
    if ((control->magic != PLATFORM_BOOT_CONTROL_MAGIC) ||
        (control->format_version != PLATFORM_BOOT_CONTROL_FORMAT_VERSION) ||
        ((control->request != (uint32_t) PLATFORM_BOOT_REQUEST_NONE) &&
         (control->request != (uint32_t) PLATFORM_BOOT_REQUEST_UPDATE)) ||
        (control->crc32 != expected_crc))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformBootControl_Write(const platform_boot_control_t *control)
{
    platform_boot_control_t stored;
    at24_status_t status;

    if (control == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((control->request != (uint32_t) PLATFORM_BOOT_REQUEST_NONE) &&
        (control->request != (uint32_t) PLATFORM_BOOT_REQUEST_UPDATE))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    stored                = *control;
    stored.magic          = PLATFORM_BOOT_CONTROL_MAGIC;
    stored.format_version = PLATFORM_BOOT_CONTROL_FORMAT_VERSION;
    stored.crc32          = CalculateCrc32(&stored, offsetof(platform_boot_control_t, crc32));

    status = At24_WritePageStart(&s_eeprom, PLATFORM_BOOT_CONTROL_EEPROM_OFFSET, &stored,
                                 (uint32_t) sizeof(stored));
    if (status != AT24_STATUS_OK)
    {
        return MapAt24Status(status);
    }
    return WaitForWrite();
}

firmware_status_t PlatformBootControl_Clear(void)
{
    platform_boot_control_t control = {0};

    control.request = (uint32_t) PLATFORM_BOOT_REQUEST_NONE;
    return PlatformBootControl_Write(&control);
}
