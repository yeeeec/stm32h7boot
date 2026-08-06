/**
 * @file boot_control_service.h
 * @brief Composition-visible Boot Control object and initialization contract.
 */
#ifndef SERVICES_BOOT_CONTROL_SERVICE_INTERNAL_H
#define SERVICES_BOOT_CONTROL_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/boot_control_store.h"
#include "firmware/checksum.h"
#include "services/capability/boot_control_service_api.h"

#define BOOT_CONTROL_MAX_RECORD_SIZE 256U

typedef struct
{
    const boot_control_store_t *store;
    const checksum_t *checksum;
} boot_control_service_dependencies_t;

typedef enum
{
    BOOT_CONTROL_STAGE_IDLE = 0,
    BOOT_CONTROL_STAGE_INVALIDATE_MARKER,
    BOOT_CONTROL_STAGE_WAIT_INVALIDATE,
    BOOT_CONTROL_STAGE_WRITE_BODY,
    BOOT_CONTROL_STAGE_WAIT_BODY,
    BOOT_CONTROL_STAGE_READ_BODY,
    BOOT_CONTROL_STAGE_WRITE_MARKER,
    BOOT_CONTROL_STAGE_WAIT_MARKER,
    BOOT_CONTROL_STAGE_VERIFY_FINAL
} boot_control_stage_t;

typedef struct boot_control_service
{
    const boot_control_store_t *store;
    const checksum_t *checksum;
    boot_control_store_info_t store_info;
    service_run_state_t state;
    service_result_t result;
    boot_control_stage_t stage;
    uint8_t write_buffer[BOOT_CONTROL_MAX_RECORD_SIZE];
    uint8_t verify_buffer[BOOT_CONTROL_MAX_RECORD_SIZE];
    uint32_t target_address;
    uint32_t record_size;
    uint32_t marker_offset;
    uint32_t write_offset;
    uint32_t last_write_size;
    int initialized;
} boot_control_service_t;

firmware_status_t BootControlService_Init(
    boot_control_service_t *service,
    const boot_control_service_dependencies_t *dependencies);

#endif
