/**
 * @file launch_service_api.h
 * @brief Validated XIP setup and final application handoff API.
 */
#ifndef SERVICES_LAUNCH_SERVICE_API_H
#define SERVICES_LAUNCH_SERVICE_API_H

#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct launch_service;

/**
 * @brief Validate the active vector table, enter XIP mode, and jump.
 *
 * @param[in,out] service Initialized Launch Service.
 * @param[in] active_record Selected and validated Boot Control Active Record.
 *
 * @return Only on failure; a successful application handoff never returns.
 */
firmware_status_t LaunchService_Execute(
    struct launch_service *service,
    const boot_active_record_t *active_record);

/** Return the result retained by the most recent launch attempt. */
const service_result_t *LaunchService_GetResult(
    const struct launch_service *service);

#endif
