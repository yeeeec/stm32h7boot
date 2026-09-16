/**
 * @file at24.h
 * @brief Hardware-independent AT24C128 EEPROM driver.
 */
#ifndef AT24_H
#define AT24_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define AT24C128_CAPACITY_BYTES   (16UL * 1024UL)
#define AT24C128_PAGE_SIZE_BYTES  64U
#define AT24C128_ADDRESS_MIN_7BIT 0x50U
#define AT24C128_ADDRESS_MAX_7BIT 0x57U

    /** Status values owned by this driver. */
    typedef enum
    {
        AT24_STATUS_OK = 0,
        AT24_STATUS_INVALID_ARGUMENT,
        AT24_STATUS_INVALID_STATE,
        AT24_STATUS_IO_ERROR,
        AT24_STATUS_TIMEOUT
    } at24_status_t;

    typedef at24_status_t (*at24_read_fn)(void *context, uint8_t device_address_7bit,
                                          uint16_t memory_address, uint8_t *data, uint32_t size);
    typedef at24_status_t (*at24_write_fn)(void *context, uint8_t device_address_7bit,
                                           uint16_t memory_address, const uint8_t *data,
                                           uint32_t size);
    typedef at24_status_t (*at24_probe_ready_fn)(void *context, uint8_t device_address_7bit,
                                                 int *ready);
    typedef uint32_t (*at24_now_ms_fn)(void *context);
    typedef at24_status_t (*at24_set_write_enabled_fn)(void *context, int enabled);

    typedef struct
    {
        void *context;
        at24_read_fn read;
        at24_write_fn write;
        at24_probe_ready_fn probe_ready;
        at24_now_ms_fn now_ms;
        /** Optional WP control; NULL means the device is externally writable. */
        at24_set_write_enabled_fn set_write_enabled;
    } at24_port_t;

    typedef struct
    {
        uint8_t device_address_7bit;
        uint32_t write_timeout_ms;
    } at24_config_t;

    typedef enum
    {
        AT24_OPERATION_IDLE = 0,
        AT24_OPERATION_BUSY,
        AT24_OPERATION_SUCCEEDED,
        AT24_OPERATION_FAILED
    } at24_operation_state_t;

    typedef struct
    {
        at24_operation_state_t state;
        at24_status_t status;
    } at24_operation_result_t;

    typedef struct
    {
        at24_port_t port;
        uint32_t write_timeout_ms;
        uint32_t write_started_ms;
        at24_status_t operation_status;
        at24_operation_state_t operation_state;
        uint8_t device_address_7bit;
        int initialized;
    } at24_t;

    at24_status_t At24_Init(at24_t *device, const at24_port_t *port, const at24_config_t *config);
    at24_status_t At24_Probe(at24_t *device);
    at24_status_t At24_Read(at24_t *device, uint32_t address, void *data, uint32_t size);
    at24_status_t At24_WritePageStart(at24_t *device, uint32_t address, const void *data,
                                      uint32_t size);
    at24_status_t At24_OperationPoll(at24_t *device);
    at24_status_t At24_GetOperationResult(const at24_t *device, at24_operation_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
