#ifndef FIRMWARE_PARAMETER_STORAGE_PORT_H
#define FIRMWARE_PARAMETER_STORAGE_PORT_H

#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Generic byte-addressed non-volatile storage. The owner task performs
     * init/read/write serialization; implementations must not expose a
     * device-driver object to Application or Services. */
    typedef struct
    {
        void *context;
        firmware_status_t (*init)(void *context);
        firmware_status_t (*read)(void *context, uint32_t address, void *data, uint32_t size);
        firmware_status_t (*write)(void *context, uint32_t address, const void *data,
                                   uint32_t size);
        uint32_t capacity_bytes;
    } parameter_storage_port_t;

#ifdef __cplusplus
}
#endif

#endif
