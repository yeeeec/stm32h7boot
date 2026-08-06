/**
 * @file update_service_api.h
 * @brief Incremental APP/GUI package installation lifecycle API.
 */
#ifndef SERVICES_UPDATE_SERVICE_API_H
#define SERVICES_UPDATE_SERVICE_API_H

#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct update_service;

/** Start one complete APP/GUI update against the inactive pair. */
firmware_status_t UpdateService_Start(
    struct update_service *service,
    const boot_active_record_t *active_record);

/** Advance at most one bounded update operation or state transition. */
void UpdateService_Process(struct update_service *service);

/** Cancel before target-slot erase begins. */
firmware_status_t UpdateService_Cancel(struct update_service *service);

/** Return the current update lifecycle state. */
service_run_state_t UpdateService_GetState(
    const struct update_service *service);

/** Return the result retained by the most recent update attempt. */
const service_result_t *UpdateService_GetResult(
    const struct update_service *service);

#endif
