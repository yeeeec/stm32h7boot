/**
 * @file active_validation_service_api.h
 * @brief Incremental active APP/GUI validation lifecycle API.
 */
#ifndef SERVICES_ACTIVE_VALIDATION_SERVICE_API_H
#define SERVICES_ACTIVE_VALIDATION_SERVICE_API_H

#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct active_validation_service;

/** Start validating the APP/GUI pair described by an Active Record. */
firmware_status_t ActiveValidationService_Start(
    struct active_validation_service *service,
    const boot_active_record_t *active_record);

/** Advance one bounded validation read or state transition. */
void ActiveValidationService_Process(
    struct active_validation_service *service);

/** Return the current validation lifecycle state. */
service_run_state_t ActiveValidationService_GetState(
    const struct active_validation_service *service);

/** Return the most recent validation result. */
const service_result_t *ActiveValidationService_GetResult(
    const struct active_validation_service *service);

#endif
