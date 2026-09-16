#include "platform/platform_flash.h"

#include <string.h>

static platform_flash_port_t s_port;

void PlatformFlash_Bind(const platform_flash_port_t *port)
{
    if (port == NULL)
        (void) memset(&s_port, 0, sizeof(s_port));
    else
        s_port = *port;
}

firmware_status_t PlatformFlash_Erase(uint32_t address, uint32_t size)
{
    return s_port.erase == NULL ? FIRMWARE_STATUS_INVALID_STATE
                                : s_port.erase(s_port.context, address, size);
}
firmware_status_t PlatformFlash_Write(uint32_t address, const void *data, size_t size)
{
    return s_port.write == NULL ? FIRMWARE_STATUS_INVALID_STATE
                                : s_port.write(s_port.context, address, data, size);
}
firmware_status_t PlatformFlash_Read(uint32_t address, void *data, size_t size)
{
    return s_port.read == NULL ? FIRMWARE_STATUS_INVALID_STATE
                               : s_port.read(s_port.context, address, data, size);
}
firmware_status_t PlatformFlash_EnterXip(void)
{
    return s_port.enter_xip == NULL ? FIRMWARE_STATUS_INVALID_STATE
                                    : s_port.enter_xip(s_port.context);
}
firmware_status_t PlatformFlash_ExitXip(void)
{
    return s_port.exit_xip == NULL ? FIRMWARE_STATUS_INVALID_STATE
                                   : s_port.exit_xip(s_port.context);
}
