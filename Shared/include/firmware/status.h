#ifndef FIRMWARE_STATUS_H
#define FIRMWARE_STATUS_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        FIRMWARE_STATUS_OK = 0,
        FIRMWARE_STATUS_INVALID_ARGUMENT,
        FIRMWARE_STATUS_INVALID_STATE,
        FIRMWARE_STATUS_NOT_SUPPORTED,
        FIRMWARE_STATUS_IO_ERROR,
        FIRMWARE_STATUS_TIMEOUT,
        FIRMWARE_STATUS_BUSY,
        FIRMWARE_STATUS_NOT_FOUND,
        FIRMWARE_STATUS_OUT_OF_RANGE,
        FIRMWARE_STATUS_BUFFER_TOO_SMALL,
        FIRMWARE_STATUS_OVERFLOW,
        FIRMWARE_STATUS_AUTHENTICATION_FAILED
    } firmware_status_t;

    static inline int FirmwareStatus_IsOk(firmware_status_t status)
    {
        return status == FIRMWARE_STATUS_OK;
    }

    static inline int FirmwareStatus_IsError(firmware_status_t status)
    {
        return status != FIRMWARE_STATUS_OK;
    }

#ifdef __cplusplus
}
#endif

#endif
