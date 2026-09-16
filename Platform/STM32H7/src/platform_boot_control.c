#include "platform/platform_boot_control.h"

#include <stddef.h>
#include <string.h>

#include "bsp/bsp_eeprom.h"

#define PLATFORM_BOOT_CONTROL_EEPROM_ADDRESS 0U

#if defined(__GNUC__)
extern firmware_status_t BSP_EepromInit(const bsp_eeprom_config_t *) __attribute__((weak));
extern firmware_status_t BSP_EepromProbe(void) __attribute__((weak));
extern firmware_status_t BSP_EepromRead(uint32_t, void *, uint32_t) __attribute__((weak));
extern firmware_status_t BSP_EepromWrite(uint32_t, const void *, uint32_t) __attribute__((weak));
#endif

static firmware_status_t eeprom_read(void *context, BootControl_t *control)
{
    (void) context;
    return BSP_EepromRead == 0
               ? FIRMWARE_STATUS_NOT_SUPPORTED
               : BSP_EepromRead(PLATFORM_BOOT_CONTROL_EEPROM_ADDRESS, control, sizeof(*control));
}
static firmware_status_t eeprom_write(void *context, const BootControl_t *control)
{
    (void) context;
    return BSP_EepromWrite == 0
               ? FIRMWARE_STATUS_NOT_SUPPORTED
               : BSP_EepromWrite(PLATFORM_BOOT_CONTROL_EEPROM_ADDRESS, control, sizeof(*control));
}

static platform_boot_control_port_t s_port;

firmware_status_t PlatformBootControl_Init(void)
{
    const bsp_eeprom_config_t config        = {0x50U, 20U};
    const platform_boot_control_port_t port = {eeprom_read, eeprom_write, NULL};
    firmware_status_t status;
    if (BSP_EepromInit == 0 || BSP_EepromProbe == 0 || BSP_EepromRead == 0 || BSP_EepromWrite == 0)
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    status = BSP_EepromInit(&config);
    if (FirmwareStatus_IsError(status))
        return status;
    status = BSP_EepromProbe();
    if (FirmwareStatus_IsError(status))
        return status;
    PlatformBootControl_Bind(&port);
    return FIRMWARE_STATUS_OK;
}

/* CRC-32 is used only for the small EEPROM metadata record, never firmware. */
static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffUL;
    size_t i;
    unsigned bit;
    for (i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320UL & (uint32_t) -(int) (crc & 1U));
    }
    return ~crc;
}

static uint32_t expected_check(const BootControl_t *control)
{
    return crc32((const uint8_t *) control, offsetof(BootControl_t, check));
}

void PlatformBootControl_Bind(const platform_boot_control_port_t *port)
{
    if (port == NULL)
        (void) memset(&s_port, 0, sizeof(s_port));
    else
        s_port = *port;
}

int PlatformBootControl_IsValid(const BootControl_t *control)
{
    if (control == NULL || control->magic != BOOT_MAGIC || control->format_version != 1U ||
        (control->request != BOOT_REQUEST_NONE && control->request != BOOT_REQUEST_UPDATE))
        return 0;
    return expected_check(control) == control->check;
}

firmware_status_t PlatformBootControl_Read(BootControl_t *control)
{
    firmware_status_t status;
    if (control == NULL || s_port.read == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = s_port.read(s_port.context, control);
    if (FirmwareStatus_IsError(status))
        return status;
    return PlatformBootControl_IsValid(control) ? FIRMWARE_STATUS_OK
                                                : FIRMWARE_STATUS_INVALID_STATE;
}

firmware_status_t PlatformBootControl_Write(const BootControl_t *control)
{
    BootControl_t copy;
    if (control == NULL || s_port.write == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    copy                = *control;
    copy.magic          = BOOT_MAGIC;
    copy.format_version = 1U;
    if (copy.request != BOOT_REQUEST_NONE && copy.request != BOOT_REQUEST_UPDATE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    copy.check = expected_check(&copy);
    return s_port.write(s_port.context, &copy);
}

firmware_status_t PlatformBootControl_Clear(void)
{
    BootControl_t control = {BOOT_MAGIC, 1U, BOOT_REQUEST_NONE, {0}, 0U};
    return PlatformBootControl_Write(&control);
}
