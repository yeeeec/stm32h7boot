/**
 * @file boot_control_service_api.h
 * @brief Boot Control A/B record selection and incremental commit API.
 */
#ifndef SERVICES_BOOT_CONTROL_SERVICE_API_H
#define SERVICES_BOOT_CONTROL_SERVICE_API_H

#include "firmware/status.h"
#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct boot_control_service;

/** Load the newest valid Active Record from the A/B store. */
firmware_status_t BootControlService_LoadActive(
    struct boot_control_service *service,
    boot_active_record_t *record);

/**
 * Start an atomic Active Record commit.
 *
 * The service assigns the next sequence number. The caller must invoke
 * BootControlService_Process until the operation reaches a terminal state.
 */
firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service,
    const boot_active_record_t *record);

/** Advance at most one EEPROM page write, readiness poll, read, or state transition. */
void BootControlService_Process(struct boot_control_service *service);

/** Return the current commit lifecycle state. */
service_run_state_t BootControlService_GetState(
    const struct boot_control_service *service);

/** Return the most recent commit result. */
const service_result_t *BootControlService_GetResult(
    const struct boot_control_service *service);

#endif
