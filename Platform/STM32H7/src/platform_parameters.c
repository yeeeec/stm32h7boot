#include "platform/platform_ports.h"

#include "bsp/bsp_eeprom.h"

#define PLATFORM_EEPROM_ADDRESS_7BIT     0x50U
#define PLATFORM_EEPROM_WRITE_TIMEOUT_MS 10U

static firmware_status_t ParameterStorageInit(void *context)
{
    const bsp_eeprom_config_t config = {
        PLATFORM_EEPROM_ADDRESS_7BIT,
        PLATFORM_EEPROM_WRITE_TIMEOUT_MS,
    };

    (void) context;
    if (BSP_EepromIsInitialized() != 0)
        return FIRMWARE_STATUS_OK;
    return BSP_EepromInit(&config);
}

static firmware_status_t ParameterStorageRead(void *context, uint32_t address, void *data,
                                              uint32_t size)
{
    (void) context;
    return BSP_EepromRead(address, data, size);
}

static firmware_status_t ParameterStorageWrite(void *context, uint32_t address,
                                               const void *data, uint32_t size)
{
    (void) context;
    return BSP_EepromWrite(address, data, size);
}

firmware_status_t Platform_GetParameterStoragePort(parameter_storage_port_t *port)
{
    if (port == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *port = (parameter_storage_port_t) {
        .context        = NULL,
        .init           = ParameterStorageInit,
        .read           = ParameterStorageRead,
        .write          = ParameterStorageWrite,
        .capacity_bytes = BSP_EEPROM_CAPACITY_BYTES,
    };
    return FIRMWARE_STATUS_OK;
}
