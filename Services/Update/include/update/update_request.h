#ifndef FIRMWARE_UPDATE_REQUEST_H
#define FIRMWARE_UPDATE_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"
#include "ports/storage_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef storage_boot_update_request_t UpdateRequest_t;

    /* Parse the bounded JSON request.  The parser accepts no unknown or duplicate
     * fields and never allocates memory. */
    firmware_status_t UpdateRequest_Parse(const uint8_t *data, size_t size,
                                          storage_boot_update_request_t *request);

    /* Validate semantic constraints required before entering the install path. */
    firmware_status_t UpdateRequest_Validate(const storage_boot_update_request_t *request);

    int UpdateRequest_IsManifestHashValid(const char *hash);

#ifdef __cplusplus
}
#endif

#endif
