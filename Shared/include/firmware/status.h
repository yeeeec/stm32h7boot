#ifndef FIRMWARE_STATUS_H
#define FIRMWARE_STATUS_H

typedef enum
{
    FIRMWARE_STATUS_OK = 0,
    FIRMWARE_STATUS_INVALID_ARGUMENT = 1,
    FIRMWARE_STATUS_INVALID_STATE = 2,
    FIRMWARE_STATUS_IO_ERROR = 3,
    FIRMWARE_STATUS_TIMEOUT = 4,
    FIRMWARE_STATUS_NOT_SUPPORTED = 5,
    FIRMWARE_STATUS_OUT_OF_RANGE = 6
} firmware_status_t;

static inline int FirmwareStatus_IsOk(firmware_status_t status)
{
    return status == FIRMWARE_STATUS_OK;
}

#endif
