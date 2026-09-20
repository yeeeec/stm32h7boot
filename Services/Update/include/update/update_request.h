#ifndef FIRMWARE_UPDATE_REQUEST_H
#define FIRMWARE_UPDATE_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define UPDATE_REQUEST_MAX_SIZE 1024U
#define UPDATE_REQUEST_PACKAGE_ID_MAX 64U
#define UPDATE_REQUEST_FILE_PATH "/UPDATE/boot_update_request.json"

    typedef enum
    {
        UPDATE_REQUEST_ABSENT = 0,
        UPDATE_REQUEST_ACTIVE,
        UPDATE_REQUEST_INVALID
    } update_request_presence_t;

    typedef struct
    {
        uint32_t format_version;
        uint8_t requested;
        char package_id[UPDATE_REQUEST_PACKAGE_ID_MAX];
        char manifest_sha256[65];
        uint32_t component_mask;
    } update_request_t;

    firmware_status_t UpdateRequest_Parse(const uint8_t *json, size_t length,
                                          update_request_t *request);
    update_request_presence_t UpdateRequest_Load(update_request_t *request);
    firmware_status_t UpdateRequest_Write(const update_request_t *request);
    firmware_status_t UpdateRequest_Delete(void);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_UPDATE_REQUEST_H */
